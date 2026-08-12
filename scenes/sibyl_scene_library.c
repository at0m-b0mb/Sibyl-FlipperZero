#include "../sibyl_i.h"

/*
 * The device library, browsable without capturing anything.
 *
 * Half the value of this app is the reference material, and needing a live
 * transmitter in front of you to read it would be a strange way to ship a
 * teaching tool. Every class the classifier can name is listed here with the
 * same explainer the result screen links to.
 */

static void sibyl_scene_library_callback(void* context, uint32_t index) {
    SibylApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

void sibyl_scene_library_on_enter(void* context) {
    SibylApp* app = context;
    Submenu* menu = app->submenu;

    submenu_reset(menu);
    submenu_set_header(menu, "Device library");

    /* Class 0 is "Unidentified", which has an entry but is not a device. */
    for(uint32_t c = 1; c < SibClassCount; c++) {
        submenu_add_item(
            menu, sib_class_name((SibClass)c), c, sibyl_scene_library_callback, app);
    }

    submenu_set_selected_item(
        menu, scene_manager_get_scene_state(app->scene_manager, SibylSceneLibrary));
    view_dispatcher_switch_to_view(app->view_dispatcher, SibylViewSubmenu);
}

bool sibyl_scene_library_on_event(void* context, SceneManagerEvent event) {
    SibylApp* app = context;
    if(event.type != SceneManagerEventTypeCustom) return false;
    if(event.event >= SibClassCount) return false;

    scene_manager_set_scene_state(app->scene_manager, SibylSceneLibrary, event.event);
    app->explain_cls = (SibClass)event.event;
    scene_manager_next_scene(app->scene_manager, SibylSceneExplain);
    return true;
}

void sibyl_scene_library_on_exit(void* context) {
    SibylApp* app = context;
    submenu_reset(app->submenu);
}
