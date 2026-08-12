#include "../sibyl_i.h"

void sibyl_scene_explain_on_enter(void* context) {
    SibylApp* app = context;
    explain_view_set_class(app->explain_view, app->explain_cls);
    view_dispatcher_switch_to_view(app->view_dispatcher, SibylViewExplain);
}

bool sibyl_scene_explain_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    return false;
}

void sibyl_scene_explain_on_exit(void* context) {
    UNUSED(context);
}
