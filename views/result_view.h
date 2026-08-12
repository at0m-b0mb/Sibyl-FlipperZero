/**
 * Sibyl - the result screen.
 *
 * Three pages, walked with left and right:
 *
 *   Answer     what it is, how sure, and why in one glance.
 *   Shortlist  everything else that fits, with its score. This page is not a
 *              consolation prize - when the signal genuinely does not
 *              distinguish a doorbell from a mains socket, this is the honest
 *              answer and the first page is the summary of it.
 *   Evidence   the measured numbers, and the captured packet drawn as a
 *              logic trace. If the trace does not look like a packet, the
 *              answer above it should not be believed, and now you can tell.
 */
#pragma once

#include "../helpers/sib_classify.h"
#include "../helpers/sib_features.h"

#include <gui/view.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Everything the screen draws, copied in one go so the view never reaches
 * back into the radio or the app. */
typedef struct {
    SibResult result;
    SibFeatures features;
    SibPulseTrain train;

    uint32_t frequency;
    SibMod mod;
    int8_t rssi;
    bool decoded;
    char protocol[28];
} ResultData;

/* Only one, deliberately: going back from the result re-enters the listening
 * scene, which restarts the radio, so "scan again" is what Back already does
 * and a second way of spelling it would just be a key that does nothing new. */
typedef enum {
    ResultViewEventExplain, /* open the explainer for the selected class */
} ResultViewEvent;

typedef void (*ResultViewCallback)(ResultViewEvent event, void* context);

typedef struct ResultView ResultView;

ResultView* result_view_alloc(void);
void result_view_free(ResultView* v);
View* result_view_get_view(ResultView* v);
void result_view_set_callback(ResultView* v, ResultViewCallback cb, void* context);

/** Load a result and reset to the first page. */
void result_view_set_data(ResultView* v, const ResultData* data);

/** Which class the user is currently pointing at, for the explainer. */
SibClass result_view_selected_class(ResultView* v);

#ifdef __cplusplus
}
#endif
