#include "listen_view.h"

#include <furi.h>
#include <gui/elements.h>

#include <stdio.h>
#include <string.h>

/* The window the trace maps onto its 30 px of height. Below the floor there is
 * nothing to see, above the ceiling a strong local transmitter would otherwise
 * flatten every other feature against the top. */
#define LV_RSSI_FLOOR_DBM (-100)
#define LV_RSSI_CEIL_DBM  (-38)

#define LV_TRACE_X    2
#define LV_TRACE_TOP  15
#define LV_TRACE_BASE 45
#define LV_TRACE_H    (LV_TRACE_BASE - LV_TRACE_TOP)

struct ListenView {
    View* view;
    ListenViewCallback cb;
    void* ctx;
};

typedef struct {
    const char* band;
    const char* mod;
    bool automod;

    int8_t trace[LISTEN_TRACE_SAMPLES];
    bool burst_at[LISTEN_TRACE_SAMPLES];
    uint8_t head; /* next write position - the trace is a ring */
    bool filled;

    uint8_t bursts;
    uint32_t rejected;
    bool decoded;
    uint32_t frame;
} ListenModel;

static uint8_t lv_bar_height(int8_t dbm) {
    if(dbm <= LV_RSSI_FLOOR_DBM) return 0;
    if(dbm >= LV_RSSI_CEIL_DBM) return LV_TRACE_H;
    int32_t span = LV_RSSI_CEIL_DBM - LV_RSSI_FLOOR_DBM;
    return (uint8_t)(((int32_t)dbm - LV_RSSI_FLOOR_DBM) * LV_TRACE_H / span);
}

static void listen_view_draw(Canvas* canvas, void* model) {
    ListenModel* m = model;
    canvas_clear(canvas);

    /* ---- header ---- */
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 10, "Listening");

    canvas_set_font(canvas, FontSecondary);
    char tune[24];
    snprintf(
        tune,
        sizeof(tune),
        "%s %s%s",
        m->band ? m->band : "---",
        m->mod ? m->mod : "--",
        m->automod ? "*" : "");
    canvas_draw_str_aligned(canvas, 126, 9, AlignRight, AlignBottom, tune);
    canvas_draw_line(canvas, 0, 12, 127, 12);

    /* ---- carrier trace ----
     * Oldest sample on the left, newest on the right, two pixels per column.
     * Drawn as a filled area so a burst reads as a solid block rather than a
     * line the eye has to follow. */
    canvas_draw_line(canvas, LV_TRACE_X, LV_TRACE_BASE, 125, LV_TRACE_BASE);

    uint8_t count = m->filled ? LISTEN_TRACE_SAMPLES : m->head;
    for(uint8_t i = 0; i < count; i++) {
        /* walk from oldest to newest */
        uint8_t idx = m->filled ? (uint8_t)((m->head + i) % LISTEN_TRACE_SAMPLES) : i;
        uint8_t col = m->filled ? i : (uint8_t)(LISTEN_TRACE_SAMPLES - count + i);
        int x = LV_TRACE_X + col * 2;

        uint8_t h = lv_bar_height(m->trace[idx]);
        if(h) canvas_draw_box(canvas, x, LV_TRACE_BASE - h, 2, h);

        /* An accepted burst gets a full-height tick above the trace, so you
         * can see exactly which bump the app decided was a packet. */
        if(m->burst_at[idx]) {
            canvas_draw_line(canvas, x, LV_TRACE_TOP - 2, x, LV_TRACE_BASE);
            canvas_draw_box(canvas, x - 1, LV_TRACE_TOP - 4, 3, 3);
        }
    }

    /* ---- counters ---- */
    canvas_set_font(canvas, FontSecondary);
    char left[26];
    if(m->bursts || m->decoded) {
        snprintf(
            left,
            sizeof(left),
            "%u packet%s%s",
            m->bursts,
            m->bursts == 1 ? "" : "s",
            m->decoded ? " decoded" : "");
    } else {
        /* A spinner that only turns while the radio is fed proves the app is
         * alive on a band where nothing is transmitting. */
        static const char* spin[4] = {"|", "/", "-", "\\"};
        snprintf(left, sizeof(left), "%s  waiting", spin[(m->frame / 3) % 4]);
    }
    canvas_draw_str(canvas, 2, 54, left);

    if(m->rejected) {
        char right[20];
        uint32_t r = m->rejected;
        if(r > 9999) r = 9999;
        snprintf(right, sizeof(right), "%lu noise", (unsigned long)r);
        canvas_draw_str_aligned(canvas, 126, 54, AlignRight, AlignBottom, right);
    }

    /* ---- footer ---- */
    canvas_draw_line(canvas, 0, 56, 127, 56);
    canvas_draw_str(canvas, 2, 63, "< > band");
    canvas_draw_str_aligned(canvas, 126, 63, AlignRight, AlignBottom, "OK reset");
}

static bool listen_view_input(InputEvent* event, void* context) {
    ListenView* v = context;
    if(event->type != InputTypeShort) return false;

    switch(event->key) {
    case InputKeyOk:
        if(v->cb) v->cb(ListenViewEventReset, v->ctx);
        return true;
    case InputKeyLeft:
        if(v->cb) v->cb(ListenViewEventBandPrev, v->ctx);
        return true;
    case InputKeyRight:
        if(v->cb) v->cb(ListenViewEventBandNext, v->ctx);
        return true;
    default:
        return false;
    }
}

ListenView* listen_view_alloc(void) {
    ListenView* v = malloc(sizeof(ListenView));
    memset(v, 0, sizeof(ListenView));

    v->view = view_alloc();
    view_allocate_model(v->view, ViewModelTypeLocking, sizeof(ListenModel));
    view_set_context(v->view, v);
    view_set_draw_callback(v->view, listen_view_draw);
    view_set_input_callback(v->view, listen_view_input);

    with_view_model(
        v->view,
        ListenModel * model,
        {
            memset(model, 0, sizeof(ListenModel));
            for(uint8_t i = 0; i < LISTEN_TRACE_SAMPLES; i++) {
                model->trace[i] = LV_RSSI_FLOOR_DBM;
            }
        },
        false);

    return v;
}

void listen_view_free(ListenView* v) {
    furi_assert(v);
    view_free(v->view);
    free(v);
}

View* listen_view_get_view(ListenView* v) {
    furi_assert(v);
    return v->view;
}

void listen_view_set_callback(ListenView* v, ListenViewCallback cb, void* context) {
    furi_assert(v);
    v->cb = cb;
    v->ctx = context;
}

void listen_view_set_tune(ListenView* v, const char* band, const char* mod, bool automod) {
    furi_assert(v);
    with_view_model(
        v->view,
        ListenModel * model,
        {
            model->band = band;
            model->mod = mod;
            model->automod = automod;
        },
        true);
}

void listen_view_push_rssi(ListenView* v, int8_t dbm) {
    furi_assert(v);
    with_view_model(
        v->view,
        ListenModel * model,
        {
            model->trace[model->head] = dbm;
            model->burst_at[model->head] = false;
            model->head = (uint8_t)((model->head + 1) % LISTEN_TRACE_SAMPLES);
            if(model->head == 0) model->filled = true;
            model->frame++;
        },
        true);
}

void listen_view_mark_burst(ListenView* v) {
    furi_assert(v);
    with_view_model(
        v->view,
        ListenModel * model,
        {
            /* The sample for the current tick has not been pushed yet, so the
             * newest written column is one behind the head. */
            uint8_t idx = (uint8_t)((model->head + LISTEN_TRACE_SAMPLES - 1) %
                                    LISTEN_TRACE_SAMPLES);
            model->burst_at[idx] = true;
        },
        true);
}

void listen_view_set_counts(ListenView* v, uint8_t bursts, uint32_t rejected, bool decoded) {
    furi_assert(v);
    with_view_model(
        v->view,
        ListenModel * model,
        {
            model->bursts = bursts;
            model->rejected = rejected;
            model->decoded = decoded;
        },
        true);
}
