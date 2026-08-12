#include "../sibyl_i.h"

typedef enum {
    StartIndexIdentify,
    StartIndexFindBand,
    StartIndexLibrary,
    StartIndexLog,
    StartIndexSettings,
    StartIndexAbout,
} StartIndex;

static void sibyl_scene_start_callback(void* context, uint32_t index) {
    SibylApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

void sibyl_scene_start_on_enter(void* context) {
    SibylApp* app = context;
    Submenu* menu = app->submenu;

    submenu_reset(menu);
    submenu_set_header(menu, "Sibyl");
    submenu_add_item(menu, "Identify signal", StartIndexIdentify, sibyl_scene_start_callback, app);
    submenu_add_item(menu, "Find band", StartIndexFindBand, sibyl_scene_start_callback, app);
    submenu_add_item(menu, "Device library", StartIndexLibrary, sibyl_scene_start_callback, app);
    submenu_add_item(menu, "This session", StartIndexLog, sibyl_scene_start_callback, app);
    submenu_add_item(menu, "Settings", StartIndexSettings, sibyl_scene_start_callback, app);
    submenu_add_item(menu, "About", StartIndexAbout, sibyl_scene_start_callback, app);

    submenu_set_selected_item(
        menu, scene_manager_get_scene_state(app->scene_manager, SibylSceneStart));

    view_dispatcher_switch_to_view(app->view_dispatcher, SibylViewSubmenu);
}

bool sibyl_scene_start_on_event(void* context, SceneManagerEvent event) {
    SibylApp* app = context;

    if(event.type != SceneManagerEventTypeCustom) return false;

    /* Remember where the cursor was, so coming back from a scene does not
     * dump you at the top of the menu again. */
    scene_manager_set_scene_state(app->scene_manager, SibylSceneStart, event.event);

    switch(event.event) {
    case StartIndexIdentify:
        scene_manager_next_scene(app->scene_manager, SibylSceneListen);
        return true;
    case StartIndexFindBand:
        scene_manager_next_scene(app->scene_manager, SibylSceneHunt);
        return true;
    case StartIndexLibrary:
        scene_manager_next_scene(app->scene_manager, SibylSceneLibrary);
        return true;
    case StartIndexLog:
        scene_manager_next_scene(app->scene_manager, SibylSceneLog);
        return true;
    case StartIndexSettings:
        scene_manager_next_scene(app->scene_manager, SibylSceneSettings);
        return true;
    case StartIndexAbout:
        scene_manager_next_scene(app->scene_manager, SibylSceneAbout);
        return true;
    default:
        return false;
    }
}

void sibyl_scene_start_on_exit(void* context) {
    SibylApp* app = context;
    submenu_reset(app->submenu);
}
