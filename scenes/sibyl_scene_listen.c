#include "../sibyl_i.h"

/*
 * Listening.
 *
 * The radio runs free; this scene samples it once per tick to draw the carrier
 * trace, and watches for the moment the air goes quiet after a packet. That
 * quiet is what ends a capture: a transmission is several copies of one packet
 * sent back to back, and analysing after the first copy would throw away the
 * repeat count, which is one of the more discriminating features there is.
 *
 * While nothing is landing and auto-modulation is on, the scene walks the
 * presets. An OOK preset cannot demodulate an FSK tyre sensor and vice versa,
 * so a fixed preset means a whole category of device is simply inaudible - and
 * the user has no way of knowing that is why they are seeing nothing.
 */

/* Ticks of silence before auto-modulation moves to the next preset. */
#define LISTEN_AUTO_HOP_TICKS 25

typedef struct {
    uint32_t idle_ticks;
    bool burst_pending;
} ListenState;

/* The scene manager gives us one 32-bit slot; that is enough for both. */
static ListenState listen_state_get(SibylApp* app) {
    uint32_t raw = scene_manager_get_scene_state(app->scene_manager, SibylSceneListen);
    ListenState s = {.idle_ticks = raw & 0x7FFFFFFFu, .burst_pending = (raw >> 31) & 1u};
    return s;
}

static void listen_state_set(SibylApp* app, ListenState s) {
    uint32_t raw = (s.idle_ticks & 0x7FFFFFFFu) | ((uint32_t)(s.burst_pending ? 1u : 0u) << 31);
    scene_manager_set_scene_state(app->scene_manager, SibylSceneListen, raw);
}

static void sibyl_listen_refresh_header(SibylApp* app) {
    uint8_t bi = app->settings.band_idx;
    if(bi >= SIB_BAND_COUNT) bi = SIB_BAND_DEFAULT;
    uint8_t mi = sibyl_active_mod_idx(app);
    if(mi >= SIB_MOD_COUNT) mi = 0;

    listen_view_set_tune(
        app->listen_view, sib_bands[bi].label, sib_mods[mi].label, app->settings.auto_mod);
}

static void sibyl_listen_restart(SibylApp* app) {
    sib_radio_stop(app->radio);
    sibyl_apply_tune(app);
    sibyl_listen_refresh_header(app);
    sib_radio_start(app->radio);
}

static void sibyl_listen_view_callback(ListenViewEvent event, void* context) {
    SibylApp* app = context;

    switch(event) {
    case ListenViewEventReset:
        sib_radio_reset_session(app->radio);
        listen_view_set_counts(app->listen_view, 0, 0, false);
        break;

    case ListenViewEventBandPrev:
        app->settings.band_idx =
            (uint8_t)((app->settings.band_idx + SIB_BAND_COUNT - 1) % SIB_BAND_COUNT);
        sib_settings_save(&app->settings);
        sibyl_listen_restart(app);
        break;

    case ListenViewEventBandNext:
        app->settings.band_idx = (uint8_t)((app->settings.band_idx + 1) % SIB_BAND_COUNT);
        sib_settings_save(&app->settings);
        sibyl_listen_restart(app);
        break;
    }

    ListenState s = {.idle_ticks = 0, .burst_pending = false};
    listen_state_set(app, s);
}

void sibyl_scene_listen_on_enter(void* context) {
    SibylApp* app = context;

    listen_view_set_callback(app->listen_view, sibyl_listen_view_callback, app);
    listen_view_set_counts(app->listen_view, 0, 0, false);

    /* Auto-modulation starts from whatever the user last chose by hand, so the
     * most likely preset is tried first rather than always AM650. */
    app->auto_mod_idx = app->settings.mod_idx;
    sibyl_apply_tune(app);
    sibyl_listen_refresh_header(app);

    ListenState s = {.idle_ticks = 0, .burst_pending = false};
    listen_state_set(app, s);

    sib_radio_start(app->radio);
    view_dispatcher_switch_to_view(app->view_dispatcher, SibylViewListen);
}

bool sibyl_scene_listen_on_event(void* context, SceneManagerEvent event) {
    SibylApp* app = context;

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == SibylCustomEventBurst) {
            ListenState s = listen_state_get(app);
            s.burst_pending = true;
            s.idle_ticks = 0;
            listen_state_set(app, s);
            sibyl_notify_burst(app);
            return true;
        }
        if(event.event == SibylCustomEventAnalyse) {
            scene_manager_next_scene(app->scene_manager, SibylSceneResult);
            return true;
        }
        return false;
    }

    if(event.type != SceneManagerEventTypeTick) return false;

    ListenState s = listen_state_get(app);

    /* draw the carrier */
    float rssi = sib_radio_rssi(app->radio);
    int8_t dbm = (rssi > 0.0f) ? 0 : ((rssi < -127.0f) ? -127 : (int8_t)rssi);
    listen_view_push_rssi(app->listen_view, dbm);
    if(s.burst_pending) {
        listen_view_mark_burst(app->listen_view);
        s.burst_pending = false;
    }

    uint8_t bursts = sib_radio_burst_count(app->radio);
    listen_view_set_counts(
        app->listen_view,
        bursts,
        sib_radio_rejected(app->radio),
        sib_radio_decoded(app->radio));

    /* The transmission is over: analyse it. */
    if(sib_radio_session_ready(app->radio)) {
        if(sibyl_analyse(app)) {
            listen_state_set(app, (ListenState){.idle_ticks = 0, .burst_pending = false});
            /* on_enter must not navigate, and neither should a tick that is
             * still inside the dispatcher's callback - post and unwind. */
            view_dispatcher_send_custom_event(app->view_dispatcher, SibylCustomEventAnalyse);
            return true;
        }
        sib_radio_reset_session(app->radio);
    }

    /* Nothing is landing. Try a different modulation - but never while there
     * is something in hand, because hopping restarts the radio and clears it. */
    s.idle_ticks++;
    if(app->settings.auto_mod && bursts == 0 && !sib_radio_decoded(app->radio) &&
       s.idle_ticks >= LISTEN_AUTO_HOP_TICKS) {
        app->auto_mod_idx = (uint8_t)((app->auto_mod_idx + 1) % SIB_MOD_COUNT);
        sibyl_listen_restart(app);
        s.idle_ticks = 0;
    }

    listen_state_set(app, s);
    return true;
}

void sibyl_scene_listen_on_exit(void* context) {
    SibylApp* app = context;
    sib_radio_stop(app->radio);
}
