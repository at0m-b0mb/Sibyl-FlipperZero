#include "sib_settings.h"

#include <storage/storage.h>
#include <toolbox/saved_struct.h>

#define TAG "Sibyl"

#define SIB_SETTINGS_PATH    APP_DATA_PATH("sibyl.conf")
#define SIB_SETTINGS_MAGIC   0x5B
#define SIB_SETTINGS_VERSION 1

void sib_settings_default(SibSettings* s) {
    furi_assert(s);
    s->band_idx = SIB_BAND_DEFAULT;
    s->mod_idx = 0; /* AM650 covers most of what is out there */
    s->auto_mod = true;
    s->sound = true;
    s->vibro = true;
    s->led = true;
}

/* A hand-edited or half-written file must never be able to index off the end
 * of sib_bands / sib_mods, so every field is re-checked on load. */
static bool sib_settings_valid(const SibSettings* s) {
    return s->band_idx < SIB_BAND_COUNT && s->mod_idx < SIB_MOD_COUNT;
}

void sib_settings_load(SibSettings* s) {
    furi_assert(s);
    sib_settings_default(s);

    SibSettings loaded;
    if(!saved_struct_load(
           SIB_SETTINGS_PATH,
           &loaded,
           sizeof(SibSettings),
           SIB_SETTINGS_MAGIC,
           SIB_SETTINGS_VERSION)) {
        FURI_LOG_D(TAG, "no saved settings, using defaults");
        return;
    }

    if(!sib_settings_valid(&loaded)) {
        FURI_LOG_W(TAG, "saved settings out of range, using defaults");
        return;
    }

    *s = loaded;
}

void sib_settings_save(const SibSettings* s) {
    furi_assert(s);

    /* The app data directory is created lazily; make sure it exists before the
     * first write, otherwise saved_struct_save has nowhere to land. */
    Storage* storage = furi_record_open(RECORD_STORAGE);
    storage_common_mkdir(storage, STORAGE_APP_DATA_PATH_PREFIX);
    furi_record_close(RECORD_STORAGE);

    if(!saved_struct_save(
           SIB_SETTINGS_PATH,
           s,
           sizeof(SibSettings),
           SIB_SETTINGS_MAGIC,
           SIB_SETTINGS_VERSION)) {
        FURI_LOG_W(TAG, "could not save settings");
    }
}
