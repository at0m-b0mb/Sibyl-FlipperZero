#include "sibyl_scene.h"

#define ADD_SCENE(prefix, name, id) prefix##_scene_##name##_on_enter,
void (*const sibyl_scene_on_enter_handlers[])(void*) = {
#include "sibyl_scene_config.h"
};
#undef ADD_SCENE

#define ADD_SCENE(prefix, name, id) prefix##_scene_##name##_on_event,
bool (*const sibyl_scene_on_event_handlers[])(void*, SceneManagerEvent) = {
#include "sibyl_scene_config.h"
};
#undef ADD_SCENE

#define ADD_SCENE(prefix, name, id) prefix##_scene_##name##_on_exit,
void (*const sibyl_scene_on_exit_handlers[])(void*) = {
#include "sibyl_scene_config.h"
};
#undef ADD_SCENE

const SceneManagerHandlers sibyl_scene_handlers = {
    .on_enter_handlers = sibyl_scene_on_enter_handlers,
    .on_event_handlers = sibyl_scene_on_event_handlers,
    .on_exit_handlers = sibyl_scene_on_exit_handlers,
    .scene_num = SibylSceneNum,
};
