/**
 * Sibyl - the explainer.
 *
 * Scrollable prose about one device class: what it is, how the radio link
 * works, what that means for its security, and what Sibyl cannot tell you
 * about it. The last section is the one that stops this being a magic answer
 * box, so it is never omitted and never abbreviated.
 */
#pragma once

#include "../helpers/sib_classify.h"

#include <gui/view.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ExplainView ExplainView;

ExplainView* explain_view_alloc(void);
void explain_view_free(ExplainView* v);
View* explain_view_get_view(ExplainView* v);

/** Show this class, scrolled back to the top. */
void explain_view_set_class(ExplainView* v, SibClass cls);

#ifdef __cplusplus
}
#endif
