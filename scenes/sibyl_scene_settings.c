#include "../sibyl_i.h"

static const char* const bool_labels[2] = {"OFF", "ON"};

static void settings_band_changed(VariableItem* item) {
    SibylApp* app = variable_item_get_context(item);
    uint8_t idx = variable_item_get_current_value_index(item);
    app->settings.band_idx = idx;
    variable_item_set_current_value_text(item, sib_bands[idx].label);
    sib_settings_save(&app->settings);
}

static void settings_mod_changed(VariableItem* item) {
    SibylApp* app = variable_item_get_context(item);
    uint8_t idx = variable_item_get_current_value_index(item);
    app->settings.mod_idx = idx;
    app->auto_mod_idx = idx;
    variable_item_set_current_value_text(item, sib_mods[idx].label);
    sib_settings_save(&app->settings);
}

static void settings_automod_changed(VariableItem* item) {
    SibylApp* app = variable_item_get_context(item);
    uint8_t idx = variable_item_get_current_value_index(item);
    app->settings.auto_mod = idx != 0;
    variable_item_set_current_value_text(item, bool_labels[idx]);
    sib_settings_save(&app->settings);
}

static void settings_sound_changed(VariableItem* item) {
    SibylApp* app = variable_item_get_context(item);
    uint8_t idx = variable_item_get_current_value_index(item);
    app->settings.sound = idx != 0;
    variable_item_set_current_value_text(item, bool_labels[idx]);
    sib_settings_save(&app->settings);
}

static void settings_vibro_changed(VariableItem* item) {
    SibylApp* app = variable_item_get_context(item);
    uint8_t idx = variable_item_get_current_value_index(item);
    app->settings.vibro = idx != 0;
    variable_item_set_current_value_text(item, bool_labels[idx]);
    sib_settings_save(&app->settings);
}

static void settings_led_changed(VariableItem* item) {
    SibylApp* app = variable_item_get_context(item);
    uint8_t idx = variable_item_get_current_value_index(item);
    app->settings.led = idx != 0;
    variable_item_set_current_value_text(item, bool_labels[idx]);
    sib_settings_save(&app->settings);
}

void sibyl_scene_settings_on_enter(void* context) {
    SibylApp* app = context;
    VariableItemList* list = app->var_item_list;
    VariableItem* item;

    variable_item_list_reset(list);

    item = variable_item_list_add(list, "Band", SIB_BAND_COUNT, settings_band_changed, app);
    variable_item_set_current_value_index(item, app->settings.band_idx);
    variable_item_set_current_value_text(item, sib_bands[app->settings.band_idx].label);

    item = variable_item_list_add(list, "Modulation", SIB_MOD_COUNT, settings_mod_changed, app);
    variable_item_set_current_value_index(item, app->settings.mod_idx);
    variable_item_set_current_value_text(item, sib_mods[app->settings.mod_idx].label);

    item = variable_item_list_add(list, "Auto mod", 2, settings_automod_changed, app);
    variable_item_set_current_value_index(item, app->settings.auto_mod ? 1 : 0);
    variable_item_set_current_value_text(item, bool_labels[app->settings.auto_mod ? 1 : 0]);

    item = variable_item_list_add(list, "Sound", 2, settings_sound_changed, app);
    variable_item_set_current_value_index(item, app->settings.sound ? 1 : 0);
    variable_item_set_current_value_text(item, bool_labels[app->settings.sound ? 1 : 0]);

    item = variable_item_list_add(list, "Vibro", 2, settings_vibro_changed, app);
    variable_item_set_current_value_index(item, app->settings.vibro ? 1 : 0);
    variable_item_set_current_value_text(item, bool_labels[app->settings.vibro ? 1 : 0]);

    item = variable_item_list_add(list, "LED", 2, settings_led_changed, app);
    variable_item_set_current_value_index(item, app->settings.led ? 1 : 0);
    variable_item_set_current_value_text(item, bool_labels[app->settings.led ? 1 : 0]);

    view_dispatcher_switch_to_view(app->view_dispatcher, SibylViewSettings);
}

bool sibyl_scene_settings_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    return false;
}

void sibyl_scene_settings_on_exit(void* context) {
    SibylApp* app = context;
    variable_item_list_reset(app->var_item_list);
}
