#include "explain_view.h"

#include "../helpers/sib_library.h"
#include "../helpers/sib_text.h"

#include <furi.h>
#include <gui/elements.h>

#include <string.h>

/* Enough for four sections of prose plus their headings and blank lines. */
#define EV_MAX_LINES 64
#define EV_ROW_H     9
#define EV_TOP       21 /* baseline of the first visible row */
#define EV_ROWS      5

struct ExplainView {
    View* view;
};

typedef struct {
    SibClass cls;
    bool built; /* lines match cls                    */
    uint8_t n_lines;
    uint8_t scroll;
    char line[EV_MAX_LINES][SIB_LINE_MAX];
    bool heading[EV_MAX_LINES];
} ExplainModel;

static void ev_push(ExplainModel* m, const char* text, bool heading) {
    if(m->n_lines >= EV_MAX_LINES) return;
    strncpy(m->line[m->n_lines], text ? text : "", SIB_LINE_MAX - 1);
    m->line[m->n_lines][SIB_LINE_MAX - 1] = '\0';
    m->heading[m->n_lines] = heading;
    m->n_lines++;
}

static void ev_push_section(Canvas* canvas, ExplainModel* m, const char* head, const char* body) {
    if(m->n_lines) ev_push(m, "", false); /* breathing room between sections */

    canvas_set_font(canvas, FontPrimary);
    ev_push(m, head, true);

    canvas_set_font(canvas, FontSecondary);
    char wrapped[EV_MAX_LINES][SIB_LINE_MAX];
    uint8_t room = (uint8_t)(EV_MAX_LINES - m->n_lines);
    if(room > EV_MAX_LINES) room = EV_MAX_LINES;
    /* 118, not 124: the scrollbar owns the last few columns. */
    uint8_t n = sib_wrap(canvas, body, 118, wrapped, room);
    for(uint8_t i = 0; i < n; i++) ev_push(m, wrapped[i], false);
}

/* Wrapping needs a canvas, so the page is laid out on the first draw after the
 * class changes rather than when it is set. */
static void ev_build(Canvas* canvas, ExplainModel* m) {
    const SibClassEntry* e = sib_class_entry(m->cls);
    m->n_lines = 0;

    ev_push_section(canvas, m, "What it is", e->what);
    ev_push_section(canvas, m, "How it works", e->how);
    ev_push_section(canvas, m, "Security", e->security);
    ev_push_section(canvas, m, "What Sibyl can't tell", e->limits);

    m->built = true;
}

static void explain_view_draw(Canvas* canvas, void* model) {
    ExplainModel* m = model;
    canvas_clear(canvas);

    if(!m->built) ev_build(canvas, m);

    canvas_set_font(canvas, FontPrimary);
    char title[SIB_LINE_MAX];
    sib_fit(canvas, sib_class_name(m->cls), 112, title, sizeof(title));
    canvas_draw_str(canvas, 2, 10, title);
    canvas_draw_line(canvas, 0, 12, 127, 12);

    uint8_t max_scroll = m->n_lines > EV_ROWS ? (uint8_t)(m->n_lines - EV_ROWS) : 0;
    if(m->scroll > max_scroll) m->scroll = max_scroll;

    for(uint8_t row = 0; row < EV_ROWS; row++) {
        uint8_t idx = (uint8_t)(m->scroll + row);
        if(idx >= m->n_lines) break;
        canvas_set_font(canvas, m->heading[idx] ? FontPrimary : FontSecondary);
        canvas_draw_str(canvas, 2, (int)(EV_TOP + row * EV_ROW_H), m->line[idx]);
    }

    if(m->n_lines > EV_ROWS) {
        elements_scrollbar_pos(canvas, 126, 14, 50, m->scroll, max_scroll + 1);
    }
}

static bool explain_view_input(InputEvent* event, void* context) {
    ExplainView* v = context;
    if(event->type != InputTypeShort && event->type != InputTypeRepeat) return false;
    if(event->key != InputKeyUp && event->key != InputKeyDown) return false;

    with_view_model(
        v->view,
        ExplainModel * model,
        {
            uint8_t max_scroll =
                model->n_lines > EV_ROWS ? (uint8_t)(model->n_lines - EV_ROWS) : 0;
            if(event->key == InputKeyDown) {
                if(model->scroll < max_scroll) model->scroll++;
            } else {
                if(model->scroll > 0) model->scroll--;
            }
        },
        true);
    return true;
}

ExplainView* explain_view_alloc(void) {
    ExplainView* v = malloc(sizeof(ExplainView));
    memset(v, 0, sizeof(ExplainView));

    v->view = view_alloc();
    view_allocate_model(v->view, ViewModelTypeLocking, sizeof(ExplainModel));
    view_set_context(v->view, v);
    view_set_draw_callback(v->view, explain_view_draw);
    view_set_input_callback(v->view, explain_view_input);

    with_view_model(
        v->view, ExplainModel * model, { memset(model, 0, sizeof(ExplainModel)); }, false);
    return v;
}

void explain_view_free(ExplainView* v) {
    furi_assert(v);
    view_free(v->view);
    free(v);
}

View* explain_view_get_view(ExplainView* v) {
    furi_assert(v);
    return v->view;
}

void explain_view_set_class(ExplainView* v, SibClass cls) {
    furi_assert(v);
    with_view_model(
        v->view,
        ExplainModel * model,
        {
            model->cls = cls;
            model->built = false;
            model->scroll = 0;
            model->n_lines = 0;
        },
        true);
}
