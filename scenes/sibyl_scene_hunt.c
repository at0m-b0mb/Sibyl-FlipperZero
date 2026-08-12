#include "../sibyl_i.h"

/*
 * Find Band.
 *
 * Sweeps every candidate frequency measuring carrier power while the user
 * holds their remote down. Each band is scored against its own noise floor, so
 * a band that is permanently busy does not win by being loud, only by getting
 * louder when the button goes down.
 *
 * OK adopts the winning band and drops straight into listening on it, which is
 * the whole point: you found the frequency, now identify what is on it.
 */

static void sibyl_hunt_view_callback(void* context) {
    SibylApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, SibylCustomEventAdoptBand);
}

void sibyl_scene_hunt_on_enter(void* context) {
    SibylApp* app = context;

    hunt_view_set_callback(app->hunt_view, sibyl_hunt_view_callback, app);
    hunt_view_update(app->hunt_view, NULL, 0, 0, -1);
    sib_radio_hunt_start(app->radio);
    view_dispatcher_switch_to_view(app->view_dispatcher, SibylViewHunt);
}

bool sibyl_scene_hunt_on_event(void* context, SceneManagerEvent event) {
    SibylApp* app = context;

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == SibylCustomEventAdoptBand) {
            int8_t best = sib_radio_hunt_best(app->radio);
            if(best >= 0 && best < (int8_t)SIB_BAND_COUNT) {
                app->settings.band_idx = (uint8_t)best;
                sib_settings_save(&app->settings);
                /* Replace this scene rather than stacking on it, so Back from
                 * listening returns to the menu instead of to a stale sweep. */
                scene_manager_search_and_switch_to_previous_scene(
                    app->scene_manager, SibylSceneStart);
                scene_manager_next_scene(app->scene_manager, SibylSceneListen);
            }
            return true;
        }
        return false;
    }

    if(event.type != SceneManagerEventTypeTick) return false;

    SibHuntBand bands[SIB_BAND_COUNT];
    uint8_t n = sib_radio_hunt_snapshot(app->radio, bands, SIB_BAND_COUNT);
    hunt_view_update(
        app->hunt_view, bands, n, sib_radio_hunt_sweeps(app->radio),
        sib_radio_hunt_best(app->radio));
    return true;
}

void sibyl_scene_hunt_on_exit(void* context) {
    SibylApp* app = context;
    sib_radio_hunt_stop(app->radio);
}
