/**
 * Sibyl - Find Band.
 *
 * Sweeps every candidate frequency measuring carrier strength while you hold
 * the remote down, and shows how far each band rose above its own noise floor.
 * Each band is judged against itself, so a noisy environment on 433 does not
 * hide a clean rise on 868.
 *
 * It reports the loudest rise and nothing else: if no band beat its floor by
 * a meaningful margin, it says so rather than pointing at the least quiet one.
 */
#pragma once

#include "../helpers/sib_radio.h"

#include <gui/view.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Fired when the user accepts the winning band. Only sent when there is one. */
typedef void (*HuntViewCallback)(void* context);

typedef struct HuntView HuntView;

HuntView* hunt_view_alloc(void);
void hunt_view_free(HuntView* v);
View* hunt_view_get_view(HuntView* v);
void hunt_view_set_callback(HuntView* v, HuntViewCallback cb, void* context);

/** Refresh with the latest sweep. `best` is an index into sib_bands, or -1. */
void hunt_view_update(
    HuntView* v,
    const SibHuntBand* bands,
    uint8_t n,
    uint32_t sweeps,
    int8_t best);

#ifdef __cplusplus
}
#endif
