#pragma once

#include <gui/scene_manager.h>

/* Generate the scene ids, handler tables and prototypes from one list, so a
 * scene can never be added in one place and forgotten in another. */
#define ADD_SCENE(prefix, name, id) SibylScene##id,
typedef enum {
#include "sibyl_scene_config.h"
    SibylSceneNum,
} SibylScene;
#undef ADD_SCENE

extern const SceneManagerHandlers sibyl_scene_handlers;

#define ADD_SCENE(prefix, name, id) void prefix##_scene_##name##_on_enter(void*);
#include "sibyl_scene_config.h"
#undef ADD_SCENE

#define ADD_SCENE(prefix, name, id) \
    bool prefix##_scene_##name##_on_event(void* context, SceneManagerEvent event);
#include "sibyl_scene_config.h"
#undef ADD_SCENE

#define ADD_SCENE(prefix, name, id) void prefix##_scene_##name##_on_exit(void* context);
#include "sibyl_scene_config.h"
#undef ADD_SCENE
