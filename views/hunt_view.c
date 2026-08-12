#include "hunt_view.h"

#include <furi.h>
#include <gui/elements.h>

#include <stdio.h>
#include <string.h>

/*
 * Sixteen bands, drawn as a bar chart rather than a list.
 *
 * The obvious layout - two columns of labelled rows - does not survive
 * contact with the display: eight rows in the 40-odd pixels available needs a
 * five pixel pitch, and FontSecondary is taller than that, so the labels sit
 * on top of each other. A bar per band sidesteps it entirely, reads as a
 * spectrum at a glance, and puts the one thing anyone actually wants - which
 * frequency won - in full size text underneath.
 */
#define HV_BAR_W    7 /* 16 bars at 8 px pitch spans the display exactly */
#define HV_BAR_PITCH 8
#define HV_BASE     46 /* bars grow upward from here                    */
#define HV_BAR_MAX  30

/* A rise of this much over a band's own floor fills the bar completely. */
#define HV_FULL_SCALE_DB 40

struct HuntView {
    View* view;
    HuntViewCallback cb;
    void* ctx;
};

typedef struct {
    SibHuntBand band[SIB_BAND_COUNT];
    uint8_t n;
    uint32_t sweeps;
    int8_t best;
} HuntModel;

static void hunt_view_draw(Canvas* canvas, void* model) {
    HuntModel* m = model;
    canvas_clear(canvas);

    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 10, "Find Band");

    /* The header carries whichever of the two is useful right now: progress
     * while the sweep is still looking, the action once it has an answer. */
    canvas_set_font(canvas, FontSecondary);
    if(m->best >= 0) {
        canvas_draw_str_aligned(canvas, 126, 10, AlignRight, AlignBottom, "OK use band");
    } else {
        char foot[28];
        uint32_t sw = m->sweeps;
        if(sw > 999) sw = 999;
        snprintf(foot, sizeof(foot), "%lu sweeps", (unsigned long)sw);
        canvas_draw_str_aligned(canvas, 126, 10, AlignRight, AlignBottom, foot);
    }
    canvas_draw_line(canvas, 0, 12, 127, 12);

    /* One bar per band, low frequency on the left. Height is the rise over
     * that band's own noise floor, so a permanently busy band does not win by
     * being loud - only by getting louder when the button goes down. */
    canvas_draw_line(canvas, 0, HV_BASE, 127, HV_BASE);

    for(uint8_t i = 0; i < m->n && i < SIB_BAND_COUNT; i++) {
        int x = i * HV_BAR_PITCH;
        const SibHuntBand* b = &m->band[i];
        bool is_best = (m->best >= 0) && (i == (uint8_t)m->best);

        int h = 0;
        if(b->seen) {
            int16_t delta = (int16_t)b->peak_dbm - (int16_t)b->floor_dbm;
            if(delta < 0) delta = 0;
            if(delta > HV_FULL_SCALE_DB) delta = HV_FULL_SCALE_DB;
            h = HV_BAR_MAX * delta / HV_FULL_SCALE_DB;
        }

        if(h > 0) {
            canvas_draw_box(canvas, x, HV_BASE - h, HV_BAR_W, h);
        } else {
            /* A band that has been sampled and heard nothing still gets a
             * mark, so "swept and quiet" is visibly not "not swept yet". */
            canvas_draw_line(canvas, x, HV_BASE - 1, x + HV_BAR_W - 1, HV_BASE - 1);
        }

        if(is_best) {
            /* Caret under the winner. Two rows only - the answer line below
             * is set in FontPrimary and its ascenders reach y=51. */
            canvas_draw_line(canvas, x + 1, HV_BASE + 2, x + HV_BAR_W - 2, HV_BASE + 2);
            canvas_draw_line(canvas, x + 2, HV_BASE + 3, x + HV_BAR_W - 3, HV_BASE + 3);
        }
    }

    /* The answer, in full size. */
    if(m->best >= 0 && m->best < (int8_t)SIB_BAND_COUNT) {
        const SibHuntBand* b = &m->band[m->best];
        int16_t delta = (int16_t)b->peak_dbm - (int16_t)b->floor_dbm;
        if(delta < 0) delta = 0;
        if(delta > 99) delta = 99;

        char hit[30];
        snprintf(hit, sizeof(hit), "%s MHz  +%d dB", sib_bands[m->best].label, (int)delta);
        canvas_set_font(canvas, FontPrimary);
        canvas_draw_str(canvas, 2, 61, hit);
    } else {
        /* No band has risen yet. Say what to do, not "no signal" - the sweep
         * only measures while the transmitter is actually transmitting. */
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str(canvas, 2, 58, "Hold your remote down");
        canvas_draw_str(canvas, 2, 63, "while this sweeps.");
    }
}

/* OK adopts the winner - but only when there is one. Offering to "use" a band
 * that never rose above its own noise would be inventing an answer. */
static bool hunt_view_input(InputEvent* event, void* context) {
    HuntView* v = context;
    if(event->type != InputTypeShort || event->key != InputKeyOk) return false;

    bool have = false;
    with_view_model(v->view, HuntModel * model, { have = model->best >= 0; }, false);
    if(have && v->cb) v->cb(v->ctx);
    return true;
}

HuntView* hunt_view_alloc(void) {
    HuntView* v = malloc(sizeof(HuntView));
    memset(v, 0, sizeof(HuntView));

    v->view = view_alloc();
    view_allocate_model(v->view, ViewModelTypeLocking, sizeof(HuntModel));
    view_set_context(v->view, v);
    view_set_draw_callback(v->view, hunt_view_draw);
    view_set_input_callback(v->view, hunt_view_input);

    with_view_model(
        v->view,
        HuntModel * model,
        {
            memset(model, 0, sizeof(HuntModel));
            model->best = -1;
        },
        false);
    return v;
}

void hunt_view_free(HuntView* v) {
    furi_assert(v);
    view_free(v->view);
    free(v);
}

View* hunt_view_get_view(HuntView* v) {
    furi_assert(v);
    return v->view;
}

void hunt_view_set_callback(HuntView* v, HuntViewCallback cb, void* context) {
    furi_assert(v);
    v->cb = cb;
    v->ctx = context;
}

void hunt_view_update(
    HuntView* v,
    const SibHuntBand* bands,
    uint8_t n,
    uint32_t sweeps,
    int8_t best) {
    furi_assert(v);
    with_view_model(
        v->view,
        HuntModel * model,
        {
            uint8_t copy = n < SIB_BAND_COUNT ? n : SIB_BAND_COUNT;
            for(uint8_t i = 0; i < copy; i++) model->band[i] = bands[i];
            model->n = copy;
            model->sweeps = sweeps;
            model->best = best;
        },
        true);
}
