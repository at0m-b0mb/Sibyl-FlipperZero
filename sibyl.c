#include "sibyl_i.h"

#include <string.h>

/* ---------------------------------------------------------- feedback ----- */

static const NotificationSequence seq_confirmed = {
    &message_green_255,
    &message_delay_250,
    &message_green_0,
    &message_note_c5,
    &message_delay_50,
    &message_note_e5,
    &message_delay_50,
    &message_note_g5,
    &message_delay_50,
    &message_sound_off,
    NULL,
};
static const NotificationSequence seq_likely = {
    &message_blue_255,
    &message_delay_250,
    &message_blue_0,
    &message_note_c5,
    &message_delay_50,
    &message_note_g5,
    &message_delay_50,
    &message_sound_off,
    NULL,
};
static const NotificationSequence seq_unsure = {
    &message_red_255,
    &message_green_255, /* red + green = amber */
    &message_delay_250,
    &message_red_0,
    &message_green_0,
    &message_note_e5,
    &message_delay_100,
    &message_sound_off,
    NULL,
};
static const NotificationSequence seq_blip_led = {
    &message_blue_255,
    &message_delay_10,
    &message_blue_0,
    NULL,
};
static const NotificationSequence seq_blip_beep = {
    &message_note_c6,
    &message_delay_10,
    &message_sound_off,
    NULL,
};
static const NotificationSequence seq_blip_vibro = {
    &message_vibro_on,
    &message_delay_10,
    &message_vibro_off,
    NULL,
};

void sibyl_notify_result(SibylApp* app, SibVerdict verdict) {
    furi_assert(app);
    if(!app->settings.led && !app->settings.sound && !app->settings.vibro) return;

    /* Sequences of different lengths cannot share a ternary - the pointer
     * types differ - so this stays an if/else chain. */
    const NotificationSequence* seq;
    if(verdict == SibVerdictConfirmed) {
        seq = &seq_confirmed;
    } else if(verdict == SibVerdictLikely) {
        seq = &seq_likely;
    } else {
        seq = &seq_unsure;
    }
    notification_message(app->notifications, seq);
}

/* One short acknowledgement per accepted packet. Each channel fires
 * separately, so someone who turned the LED off but left sound on is still
 * told that something landed. */
void sibyl_notify_burst(SibylApp* app) {
    furi_assert(app);
    if(app->settings.led) notification_message(app->notifications, &seq_blip_led);
    if(app->settings.sound) notification_message(app->notifications, &seq_blip_beep);
    if(app->settings.vibro) notification_message(app->notifications, &seq_blip_vibro);
}

/* ------------------------------------------------------------- tuning ---- */

uint8_t sibyl_active_mod_idx(SibylApp* app) {
    furi_assert(app);
    return app->settings.auto_mod ? app->auto_mod_idx : app->settings.mod_idx;
}

void sibyl_apply_tune(SibylApp* app) {
    furi_assert(app);
    uint8_t mi = sibyl_active_mod_idx(app);
    if(mi >= SIB_MOD_COUNT) mi = 0;
    uint8_t bi = app->settings.band_idx;
    if(bi >= SIB_BAND_COUNT) bi = SIB_BAND_DEFAULT;

    sib_radio_configure(
        app->radio, sib_bands[bi].frequency, sib_mods[mi].preset, sib_mods[mi].mod);
}

/* ---------------------------------------------------------- analysis ----- */

bool sibyl_analyse(SibylApp* app) {
    furi_assert(app);

    if(!sib_radio_snapshot(app->radio, &app->session)) return false;

    /* Timing comes from the raw capture; a decode with no accepted burst still
     * classifies, just without the fingerprint half of the evidence. */
    if(app->session.n_bursts > 0) {
        sib_features_extract(
            &app->session.train, app->session.burst, app->session.n_bursts, &app->features);
    } else {
        memset(&app->features, 0, sizeof(app->features));
    }

    /* Take the modulation from the preset the session was actually captured
     * under, not from whatever the settings say now: auto-modulation may have
     * moved on between the capture and this call. */
    SibMod mod = SibModOok;
    for(uint8_t i = 0; i < SIB_MOD_COUNT; i++) {
        if(sib_mods[i].preset == app->session.preset) {
            mod = sib_mods[i].mod;
            break;
        }
    }

    SibContext ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.frequency_hz = app->session.frequency;
    ctx.mod = mod;
    ctx.rssi_dbm = app->session.rssi;
    ctx.decoded = app->session.decoded;
    ctx.proto_static = app->session.proto_static;
    ctx.proto_dynamic = app->session.proto_dynamic;
    strncpy(ctx.protocol, app->session.protocol, sizeof(ctx.protocol) - 1);
    ctx.protocol[sizeof(ctx.protocol) - 1] = '\0';

    sib_classify(&app->features, &ctx, &app->result);

    /* Pack everything the result screen draws into one copyable blob. */
    memset(&app->result_data, 0, sizeof(app->result_data));
    app->result_data.result = app->result;
    app->result_data.features = app->features;
    app->result_data.train = app->session.train;
    app->result_data.frequency = app->session.frequency;
    app->result_data.mod = ctx.mod;
    app->result_data.rssi = app->session.rssi;
    app->result_data.decoded = app->session.decoded;
    strncpy(app->result_data.protocol, app->session.protocol,
            sizeof(app->result_data.protocol) - 1);

    app->have_result = true;

    /* Log it. The log is short and newest-last, so an old entry falls off the
     * front rather than the newest one being dropped. */
    if(app->log_count == SIBYL_LOG_MAX) {
        memmove(&app->log[0], &app->log[1], sizeof(SibylLogEntry) * (SIBYL_LOG_MAX - 1));
        app->log_count--;
    }
    SibylLogEntry* e = &app->log[app->log_count++];
    e->cls = app->result.n_cand ? app->result.cand[0].cls : SibClassUnknown;
    if(app->result.verdict == SibVerdictUnknown) e->cls = SibClassUnknown;
    e->verdict = app->result.verdict;
    e->confidence = app->result.confidence;
    e->frequency = app->session.frequency;

    return true;
}

/* ------------------------------------------------ view dispatcher glue ---- */

static bool sibyl_custom_event_callback(void* context, uint32_t event) {
    SibylApp* app = context;
    return scene_manager_handle_custom_event(app->scene_manager, event);
}

static bool sibyl_back_event_callback(void* context) {
    SibylApp* app = context;
    return scene_manager_handle_back_event(app->scene_manager);
}

static void sibyl_tick_event_callback(void* context) {
    SibylApp* app = context;
    scene_manager_handle_tick_event(app->scene_manager);
}

/* --------------------------------------------------------- lifecycle ----- */

static SibylApp* sibyl_app_alloc(void) {
    SibylApp* app = malloc(sizeof(SibylApp));
    memset(app, 0, sizeof(SibylApp));

    app->gui = furi_record_open(RECORD_GUI);
    app->notifications = furi_record_open(RECORD_NOTIFICATION);

    app->view_dispatcher = view_dispatcher_alloc();
    app->scene_manager = scene_manager_alloc(&sibyl_scene_handlers, app);

    view_dispatcher_set_event_callback_context(app->view_dispatcher, app);
    view_dispatcher_set_custom_event_callback(app->view_dispatcher, sibyl_custom_event_callback);
    view_dispatcher_set_navigation_event_callback(app->view_dispatcher, sibyl_back_event_callback);
    /* 100 ms: fast enough for a readable carrier trace, slow enough that the
     * SPI read behind it never competes with the decoder for the bus. */
    view_dispatcher_set_tick_event_callback(app->view_dispatcher, sibyl_tick_event_callback, 100);

    sib_settings_load(&app->settings);
    app->auto_mod_idx = app->settings.mod_idx;

    /* The radio posts using the app's own event id - one definition means the
     * listen scene and the worker can never drift apart. */
    app->radio = sib_radio_alloc(app->view_dispatcher, SibylCustomEventBurst);

    app->submenu = submenu_alloc();
    view_dispatcher_add_view(app->view_dispatcher, SibylViewSubmenu, submenu_get_view(app->submenu));

    app->var_item_list = variable_item_list_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, SibylViewSettings, variable_item_list_get_view(app->var_item_list));

    app->widget = widget_alloc();
    view_dispatcher_add_view(app->view_dispatcher, SibylViewWidget, widget_get_view(app->widget));

    app->listen_view = listen_view_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, SibylViewListen, listen_view_get_view(app->listen_view));

    app->result_view = result_view_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, SibylViewResult, result_view_get_view(app->result_view));

    app->explain_view = explain_view_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, SibylViewExplain, explain_view_get_view(app->explain_view));

    app->hunt_view = hunt_view_alloc();
    view_dispatcher_add_view(app->view_dispatcher, SibylViewHunt, hunt_view_get_view(app->hunt_view));

    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);
    return app;
}

static void sibyl_app_free(SibylApp* app) {
    furi_assert(app);

    sib_radio_hunt_stop(app->radio);
    sib_radio_stop(app->radio);

    view_dispatcher_remove_view(app->view_dispatcher, SibylViewSubmenu);
    view_dispatcher_remove_view(app->view_dispatcher, SibylViewSettings);
    view_dispatcher_remove_view(app->view_dispatcher, SibylViewWidget);
    view_dispatcher_remove_view(app->view_dispatcher, SibylViewListen);
    view_dispatcher_remove_view(app->view_dispatcher, SibylViewResult);
    view_dispatcher_remove_view(app->view_dispatcher, SibylViewExplain);
    view_dispatcher_remove_view(app->view_dispatcher, SibylViewHunt);

    submenu_free(app->submenu);
    variable_item_list_free(app->var_item_list);
    widget_free(app->widget);
    listen_view_free(app->listen_view);
    result_view_free(app->result_view);
    explain_view_free(app->explain_view);
    hunt_view_free(app->hunt_view);

    view_dispatcher_free(app->view_dispatcher);
    scene_manager_free(app->scene_manager);

    sib_radio_free(app->radio);

    furi_record_close(RECORD_NOTIFICATION);
    furi_record_close(RECORD_GUI);

    free(app);
}

int32_t sibyl_app(void* p) {
    UNUSED(p);
    SibylApp* app = sibyl_app_alloc();
    scene_manager_next_scene(app->scene_manager, SibylSceneStart);
    view_dispatcher_run(app->view_dispatcher);
    sibyl_app_free(app);
    return 0;
}
