/**
 * Sibyl - persistent settings.
 *
 * Band, modulation and feedback survive between runs, so the frequency you
 * actually care about is the one the app opens on. Stored as a versioned
 * struct in the app's own data directory; a missing, corrupt or older file
 * falls back to defaults rather than failing to start.
 */
#pragma once

#include "sib_radio.h"

#include <furi.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t band_idx; /* index into sib_bands            */
    uint8_t mod_idx; /* index into sib_mods             */
    bool auto_mod; /* cycle presets while nothing lands */
    bool sound;
    bool vibro;
    bool led;
} SibSettings;

/** Shipped defaults: 433.92 MHz, AM650, auto modulation on, feedback on. */
void sib_settings_default(SibSettings* s);

/** Load from disk, falling back to defaults. Always leaves `s` usable. */
void sib_settings_load(SibSettings* s);

/** Persist. Best effort - a failed write is not worth an error dialog. */
void sib_settings_save(const SibSettings* s);

#ifdef __cplusplus
}
#endif
