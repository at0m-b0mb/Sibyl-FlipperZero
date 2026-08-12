#include "../sibyl_i.h"

#include <stdio.h>

/*
 * What this session has identified so far.
 *
 * Kept in RAM only and deliberately so: a running list of what transmits
 * around someone, written to their SD card, is a surveillance log. It is
 * useful for the twenty minutes you are walking round a building, and it
 * should not outlive the app.
 */

static void sibyl_scene_log_callback(void* context, uint32_t index) {
    SibylApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

void sibyl_scene_log_on_enter(void* context) {
    SibylApp* app = context;
    Submenu* menu = app->submenu;

    submenu_reset(menu);
    submenu_set_header(menu, "This session");

    if(app->log_count == 0) {
        submenu_add_item(menu, "Nothing identified yet", 0xFF, sibyl_scene_log_callback, app);
    } else {
        /* Newest first: the thing you just captured is the thing you want. */
        for(uint8_t i = 0; i < app->log_count; i++) {
            uint8_t idx = (uint8_t)(app->log_count - 1 - i);
            const SibylLogEntry* e = &app->log[idx];

            char label[42];
            uint32_t mhz = e->frequency / 1000000u;
            uint32_t frac = (e->frequency % 1000000u) / 10000u;
            if(mhz > 999) mhz = 999;
            if(frac > 99) frac = 99;
            uint8_t conf = e->confidence > 100 ? 100 : e->confidence;

            snprintf(
                label,
                sizeof(label),
                "%s %lu.%02lu %u%%",
                sib_class_name(e->cls),
                (unsigned long)mhz,
                (unsigned long)frac,
                conf);
            submenu_add_item(menu, label, idx, sibyl_scene_log_callback, app);
        }
    }

    view_dispatcher_switch_to_view(app->view_dispatcher, SibylViewSubmenu);
}

bool sibyl_scene_log_on_event(void* context, SceneManagerEvent event) {
    SibylApp* app = context;
    if(event.type != SceneManagerEventTypeCustom) return false;
    if(event.event >= app->log_count) return true; /* the placeholder row */

    app->explain_cls = app->log[event.event].cls;
    scene_manager_next_scene(app->scene_manager, SibylSceneExplain);
    return true;
}

void sibyl_scene_log_on_exit(void* context) {
    SibylApp* app = context;
    submenu_reset(app->submenu);
}
