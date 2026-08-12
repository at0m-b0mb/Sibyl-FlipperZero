/**
 * Sibyl - the listening screen.
 *
 * Draws a live trace of carrier strength scrolling right to left, with a mark
 * on every burst the segmenter accepted. That is not decoration: it is the
 * difference between "nothing is happening" and "something is happening on
 * this band but Sibyl cannot make a packet out of it", which are the two
 * situations a user needs to tell apart before they start changing settings.
 */
#pragma once

#include <gui/view.h>

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* One sample column per tick; the trace is two pixels wide per sample. */
#define LISTEN_TRACE_SAMPLES 62

typedef enum {
    ListenViewEventReset, /* discard and keep listening        */
    ListenViewEventBandPrev,
    ListenViewEventBandNext,
} ListenViewEvent;

typedef void (*ListenViewCallback)(ListenViewEvent event, void* context);

typedef struct ListenView ListenView;

ListenView* listen_view_alloc(void);
void listen_view_free(ListenView* v);
View* listen_view_get_view(ListenView* v);
void listen_view_set_callback(ListenView* v, ListenViewCallback cb, void* context);

/** Band and modulation labels shown in the header. Not copied - keep alive. */
void listen_view_set_tune(ListenView* v, const char* band, const char* mod, bool automod);

/** Push one carrier-strength sample onto the trace. Call on every tick. */
void listen_view_push_rssi(ListenView* v, int8_t dbm);

/** Mark the newest trace column as a burst that passed the acceptance test. */
void listen_view_mark_burst(ListenView* v);

/** Counters shown under the trace. */
void listen_view_set_counts(ListenView* v, uint8_t bursts, uint32_t rejected, bool decoded);

#ifdef __cplusplus
}
#endif
