#include "../sibyl_i.h"

static void sibyl_result_view_callback(ResultViewEvent event, void* context) {
    SibylApp* app = context;

    switch(event) {
    case ResultViewEventExplain:
        app->explain_cls = result_view_selected_class(app->result_view);
        view_dispatcher_send_custom_event(app->view_dispatcher, SibylCustomEventExplain);
        break;
    case ResultViewEventRescan:
        view_dispatcher_send_custom_event(app->view_dispatcher, SibylCustomEventRescan);
        break;
    }
}

void sibyl_scene_result_on_enter(void* context) {
    SibylApp* app = context;

    result_view_set_callback(app->result_view, sibyl_result_view_callback, app);
    result_view_set_data(app->result_view, &app->result_data);

    sibyl_notify_result(app, app->result.verdict);

    view_dispatcher_switch_to_view(app->view_dispatcher, SibylViewResult);
}

bool sibyl_scene_result_on_event(void* context, SceneManagerEvent event) {
    SibylApp* app = context;
    if(event.type != SceneManagerEventTypeCustom) return false;

    switch(event.event) {
    case SibylCustomEventExplain:
        scene_manager_next_scene(app->scene_manager, SibylSceneExplain);
        return true;
    case SibylCustomEventRescan:
        scene_manager_previous_scene(app->scene_manager);
        return true;
    default:
        return false;
    }
}

void sibyl_scene_result_on_exit(void* context) {
    UNUSED(context);
}
