#include "result_view.h"

#include "../helpers/sib_library.h"
#include "../helpers/sib_text.h"
#include "sibyl_icons.h"

#include <furi.h>
#include <gui/elements.h>

#include <stdio.h>
#include <string.h>

#define RV_PAGES 3

struct ResultView {
    View* view;
    ResultViewCallback cb;
    void* ctx;
};

typedef struct {
    ResultData d;
    bool has;
    uint8_t page;
    uint8_t sel; /* index into d.result.cand */
} ResultModel;

/* One glyph per class, in SibClass order. */
static const Icon* rv_icon(SibClass cls) {
    switch(cls) {
    case SibClassGateRemote:
        return &I_dev_gate_13px;
    case SibClassCarFob:
        return &I_dev_car_13px;
    case SibClassTpms:
        return &I_dev_tpms_13px;
    case SibClassWeather:
        return &I_dev_weather_13px;
    case SibClassDoorbell:
        return &I_dev_bell_13px;
    case SibClassSocket:
        return &I_dev_socket_13px;
    case SibClassSensor:
        return &I_dev_sensor_13px;
    case SibClassBlinds:
        return &I_dev_blinds_13px;
    case SibClassMeter:
        return &I_dev_meter_13px;
    case SibClassIndustrial:
        return &I_dev_industrial_13px;
    default:
        return &I_dev_unknown_13px;
    }
}

/* Inverted pill, used for the verdict word. Returns the width consumed. */
static int rv_badge(Canvas* canvas, int x, int y, const char* text) {
    canvas_set_font(canvas, FontSecondary);
    int w = canvas_string_width(canvas, text) + 6;
    canvas_draw_rbox(canvas, x, y, w, 11, 2);
    canvas_set_color(canvas, ColorWhite);
    canvas_draw_str(canvas, x + 3, y + 8, text);
    canvas_set_color(canvas, ColorBlack);
    return w;
}

static void rv_meter(Canvas* canvas, int x, int y, int w, int h, uint8_t pct) {
    canvas_draw_frame(canvas, x, y, w, h);
    if(pct > 100) pct = 100;
    int fill = (w - 2) * pct / 100;
    if(fill > 0) canvas_draw_box(canvas, x + 1, y + 1, fill, h - 2);
}

static void rv_page_dots(Canvas* canvas, uint8_t page) {
    for(uint8_t i = 0; i < RV_PAGES; i++) {
        int x = 108 + i * 6;
        if(i == page) {
            canvas_draw_disc(canvas, x, 60, 2);
        } else {
            canvas_draw_circle(canvas, x, 60, 2);
        }
    }
}

static void rv_freq_str(uint32_t hz, SibMod mod, char* out, size_t sz) {
    uint32_t mhz = hz / 1000000u;
    uint32_t frac = (hz % 1000000u) / 10000u;
    if(mhz > 999) mhz = 999;
    if(frac > 99) frac = 99;
    snprintf(out, sz, "%lu.%02lu %s", (unsigned long)mhz, (unsigned long)frac,
             mod == SibModFsk ? "FM" : "AM");
}

/* ------------------------------------------------------------- page 1 ---- */

static void rv_draw_answer(Canvas* canvas, ResultModel* m) {
    const SibResult* r = &m->d.result;
    SibClass top = r->n_cand ? r->cand[0].cls : SibClassUnknown;
    if(r->verdict == SibVerdictUnknown) top = SibClassUnknown;

    /* header: verdict pill on the left, tune on the right */
    rv_badge(canvas, 2, 1, sib_verdict_label(r->verdict));

    canvas_set_font(canvas, FontSecondary);
    char tune[20];
    rv_freq_str(m->d.frequency, m->d.mod, tune, sizeof(tune));
    canvas_draw_str_aligned(canvas, 126, 10, AlignRight, AlignBottom, tune);

    /* the answer */
    canvas_draw_icon(canvas, 2, 15, rv_icon(top));

    canvas_set_font(canvas, FontPrimary);
    char name[SIB_LINE_MAX];
    sib_fit(canvas, sib_class_name(top), 105, name, sizeof(name));
    canvas_draw_str(canvas, 19, 25, name);

    /* Two lines at an 8 px pitch, so a tagline that wraps still clears the
     * confidence bar underneath it. */
    canvas_set_font(canvas, FontSecondary);
    char lines[2][SIB_LINE_MAX];
    uint8_t n = sib_wrap(canvas, sib_class_tagline(top), 124, lines, 2);
    for(uint8_t i = 0; i < n; i++) {
        canvas_draw_str(canvas, 2, 35 + i * 8, lines[i]);
    }

    /* how sure */
    rv_meter(canvas, 2, 46, 96, 8, r->confidence);
    char pct[8];
    uint8_t c = r->confidence > 100 ? 100 : r->confidence;
    snprintf(pct, sizeof(pct), "%u%%", c);
    canvas_draw_str_aligned(canvas, 126, 53, AlignRight, AlignBottom, pct);

    canvas_draw_str(canvas, 2, 63, "OK explain");
    rv_page_dots(canvas, m->page);
}

/* ------------------------------------------------------------- page 2 ---- */

static void rv_draw_shortlist(Canvas* canvas, ResultModel* m) {
    const SibResult* r = &m->d.result;

    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 10, "What it could be");
    canvas_draw_line(canvas, 0, 12, 127, 12);

    canvas_set_font(canvas, FontSecondary);

    if(r->n_cand == 0) {
        canvas_draw_str(canvas, 2, 26, "Nothing scored high enough");
        canvas_draw_str(canvas, 2, 36, "to be worth listing.");
        rv_page_dots(canvas, m->page);
        return;
    }

    for(uint8_t i = 0; i < r->n_cand; i++) {
        int y = 15 + i * 10;
        bool sel = (i == m->sel);

        if(sel) {
            canvas_draw_box(canvas, 0, y, 128, 10);
            canvas_set_color(canvas, ColorWhite);
        }

        /* 56 px, not 62: the score bar starts at 60 and a name fitted right up
         * to it collides with the frame. */
        char nm[SIB_LINE_MAX];
        sib_fit(canvas, sib_class_name(r->cand[i].cls), 56, nm, sizeof(nm));
        canvas_draw_str(canvas, 3, y + 8, nm);

        char sc[8];
        uint8_t s = r->cand[i].score > 100 ? 100 : r->cand[i].score;
        snprintf(sc, sizeof(sc), "%u", s);
        canvas_draw_str_aligned(canvas, 125, y + 8, AlignRight, AlignBottom, sc);

        /* mini score bar between the name and the number */
        int bw = 30;
        int bx = 90 - bw;
        canvas_draw_frame(canvas, bx, y + 2, bw, 6);
        int fill = (bw - 2) * s / 100;
        if(fill > 0) canvas_draw_box(canvas, bx + 1, y + 3, fill, 4);

        if(sel) canvas_set_color(canvas, ColorBlack);
    }

    /* The point of the whole app, stated where it matters most. Four rows
     * reach y=55, so there is no separator here and the footer text has to
     * stay clear of the page dots at x=106. */
    if(r->generic_encoder) {
        canvas_draw_str(canvas, 2, 63, "Chip, not device");
    } else {
        canvas_draw_str(canvas, 2, 63, "OK explain");
    }
    rv_page_dots(canvas, m->page);
}

/* ------------------------------------------------------------- page 3 ---- */

/*
 * Draw the captured packet as a logic trace, scaled so the whole reference
 * burst fits the width. Compressed to unreadability on a long packet is the
 * correct outcome - the density itself tells you the packet was long - and it
 * is still obvious at a glance whether what was captured has the blocky
 * structure of a code or the hash of noise.
 */
static void rv_draw_trace(Canvas* canvas, const SibPulseTrain* t, int x0, int y_lo, int y_hi, int w) {
    if(t->n == 0) return;

    uint32_t total = 0;
    for(uint16_t i = 0; i < t->n; i++) total += t->dur[i];
    if(total == 0) return;

    bool level = t->first_level;
    uint32_t acc = 0;
    int prev_x = x0;

    for(uint16_t i = 0; i < t->n; i++) {
        acc += t->dur[i];
        int x = x0 + (int)((uint64_t)acc * (uint32_t)w / total);
        if(x > x0 + w) x = x0 + w;
        int y = level ? y_hi : y_lo;

        canvas_draw_line(canvas, prev_x, y, x, y);
        canvas_draw_line(canvas, x, y_lo, x, y_hi); /* the edge */

        prev_x = x;
        level = !level;
    }
}

static void rv_draw_evidence(Canvas* canvas, ResultModel* m) {
    const SibResult* r = &m->d.result;
    const SibFeatures* f = &m->d.features;

    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 10, "Evidence");

    canvas_set_font(canvas, FontSecondary);
    char rssi[14];
    int8_t dbm = m->d.rssi;
    if(dbm < -127) dbm = -127;
    if(dbm > 0) dbm = 0;
    snprintf(rssi, sizeof(rssi), "%d dBm", (int)dbm);
    canvas_draw_str_aligned(canvas, 126, 10, AlignRight, AlignBottom, rssi);
    canvas_draw_line(canvas, 0, 12, 127, 12);

    /* Four text rows fit between the header and the trace. When the library
     * had something to say about the protocol it takes the first two, and
     * reason[0] is skipped - it only restates in three words what the note
     * has just said in full. */
    uint8_t y = 20;
    uint8_t first_reason = 0;
    if(r->proto_note[0]) {
        char lines[2][SIB_LINE_MAX];
        uint8_t n = sib_wrap(canvas, r->proto_note, 124, lines, 2);
        for(uint8_t i = 0; i < n; i++) {
            canvas_draw_str(canvas, 2, y, lines[i]);
            y = (uint8_t)(y + 8);
        }
        first_reason = 1;
    }
    for(uint8_t i = first_reason; i < r->n_reason && y <= 44; i++) {
        canvas_draw_str(canvas, 2, y, r->reason[i]);
        y = (uint8_t)(y + 8);
    }

    /* the captured packet */
    if(f->valid && m->d.train.n) {
        canvas_draw_line(canvas, 0, 46, 127, 46);
        rv_draw_trace(canvas, &m->d.train, 2, 56, 50, 122);

        char cap[26];
        uint16_t np = m->d.train.n;
        if(np > 999) np = 999;
        snprintf(cap, sizeof(cap), "%u edges%s", np, m->d.train.truncated ? "+" : "");
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str(canvas, 2, 63, cap);
    } else {
        canvas_draw_str(canvas, 2, 63, "Nothing captured");
    }

    rv_page_dots(canvas, m->page);
}

/* ------------------------------------------------------------ plumbing --- */

static void result_view_draw(Canvas* canvas, void* model) {
    ResultModel* m = model;
    canvas_clear(canvas);
    if(!m->has) return;

    switch(m->page) {
    case 1:
        rv_draw_shortlist(canvas, m);
        break;
    case 2:
        rv_draw_evidence(canvas, m);
        break;
    default:
        rv_draw_answer(canvas, m);
        break;
    }
}

static bool result_view_input(InputEvent* event, void* context) {
    ResultView* v = context;
    if(event->type != InputTypeShort && event->type != InputTypeRepeat) return false;

    bool handled = false;
    bool explain = false;

    with_view_model(
        v->view,
        ResultModel * model,
        {
            switch(event->key) {
            case InputKeyRight:
                if(model->page + 1 < RV_PAGES) model->page++;
                handled = true;
                break;
            case InputKeyLeft:
                if(model->page > 0) model->page--;
                handled = true;
                break;
            case InputKeyUp:
                if(model->page == 1 && model->sel > 0) model->sel--;
                handled = true;
                break;
            case InputKeyDown:
                if(model->page == 1 && model->sel + 1 < model->d.result.n_cand) {
                    model->sel++;
                }
                handled = true;
                break;
            case InputKeyOk:
                explain = model->d.result.n_cand > 0;
                handled = true;
                break;
            default:
                break;
            }
        },
        true);

    if(explain && v->cb) v->cb(ResultViewEventExplain, v->ctx);
    return handled;
}

ResultView* result_view_alloc(void) {
    ResultView* v = malloc(sizeof(ResultView));
    memset(v, 0, sizeof(ResultView));

    v->view = view_alloc();
    view_allocate_model(v->view, ViewModelTypeLocking, sizeof(ResultModel));
    view_set_context(v->view, v);
    view_set_draw_callback(v->view, result_view_draw);
    view_set_input_callback(v->view, result_view_input);

    with_view_model(v->view, ResultModel * model, { memset(model, 0, sizeof(ResultModel)); }, false);
    return v;
}

void result_view_free(ResultView* v) {
    furi_assert(v);
    view_free(v->view);
    free(v);
}

View* result_view_get_view(ResultView* v) {
    furi_assert(v);
    return v->view;
}

void result_view_set_callback(ResultView* v, ResultViewCallback cb, void* context) {
    furi_assert(v);
    v->cb = cb;
    v->ctx = context;
}

void result_view_set_data(ResultView* v, const ResultData* data) {
    furi_assert(v);
    furi_assert(data);
    with_view_model(
        v->view,
        ResultModel * model,
        {
            memcpy(&model->d, data, sizeof(ResultData));
            model->has = true;
            model->page = 0;
            model->sel = 0;
        },
        true);
}

SibClass result_view_selected_class(ResultView* v) {
    furi_assert(v);
    SibClass cls = SibClassUnknown;
    with_view_model(
        v->view,
        ResultModel * model,
        {
            /* On the answer and evidence pages the subject is the top
             * candidate; only the shortlist lets you point somewhere else. */
            uint8_t idx = (model->page == 1) ? model->sel : 0;
            if(idx < model->d.result.n_cand) cls = model->d.result.cand[idx].cls;
            if(model->d.result.verdict == SibVerdictUnknown && model->page != 1) {
                cls = SibClassUnknown;
            }
        },
        false);
    return cls;
}
