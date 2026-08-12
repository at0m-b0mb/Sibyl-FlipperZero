#pragma once

#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <gui/view_dispatcher.h>
#include <gui/scene_manager.h>
#include <gui/modules/submenu.h>
#include <gui/modules/variable_item_list.h>
#include <gui/modules/widget.h>
#include <notification/notification.h>
#include <notification/notification_messages.h>

#include "sibyl_icons.h" // generated from icons/ by fbt

#include "helpers/sib_classify.h"
#include "helpers/sib_features.h"
#include "helpers/sib_library.h"
#include "helpers/sib_radio.h"
#include "helpers/sib_settings.h"
#include "helpers/sib_text.h"
#include "views/explain_view.h"
#include "views/hunt_view.h"
#include "views/listen_view.h"
#include "views/result_view.h"
#include "scenes/sibyl_scene.h"

#define SIBYL_VERSION "1.0"

/* How many identifications the session log remembers. */
#define SIBYL_LOG_MAX 8

typedef enum {
    SibylViewSubmenu,
    SibylViewListen,
    SibylViewResult,
    SibylViewExplain,
    SibylViewHunt,
    SibylViewSettings,
    SibylViewWidget,
} SibylViewId;

typedef enum {
    SibylCustomEventBurst = 100, /* radio accepted a packet          */
    SibylCustomEventAnalyse, /* transmission over -> classify    */
    SibylCustomEventExplain, /* open the explainer               */
    SibylCustomEventRescan, /* listen again                     */
    SibylCustomEventAdoptBand, /* Find Band found one -> use it    */
} SibylCustomEvent;

/* One line of the session log. */
typedef struct {
    SibClass cls;
    SibVerdict verdict;
    uint8_t confidence;
    uint32_t frequency;
} SibylLogEntry;

typedef struct {
    Gui* gui;
    ViewDispatcher* view_dispatcher;
    SceneManager* scene_manager;
    NotificationApp* notifications;

    Submenu* submenu;
    VariableItemList* var_item_list;
    Widget* widget;
    ListenView* listen_view;
    ResultView* result_view;
    ExplainView* explain_view;
    HuntView* hunt_view;

    SibRadio* radio;

    /* persisted across launches */
    SibSettings settings;

    /* the capture being worked on */
    SibSession session;
    SibFeatures features;
    SibResult result;
    ResultData result_data;
    bool have_result;

    /* which class the explainer is showing */
    SibClass explain_cls;

    /* session log, newest last */
    SibylLogEntry log[SIBYL_LOG_MAX];
    uint8_t log_count;

    /* Auto-modulation walks the presets while nothing is landing; this is the
     * index it is currently parked on. */
    uint8_t auto_mod_idx;
} SibylApp;

/* Tune the radio from the current settings (and auto-modulation position). */
void sibyl_apply_tune(SibylApp* app);

/* Which modulation preset is actually in force right now. */
uint8_t sibyl_active_mod_idx(SibylApp* app);

/* Run the classifier over whatever the radio captured. Returns false when
 * there was nothing to analyse. */
bool sibyl_analyse(SibylApp* app);

/* Feedback, all gated by settings. */
void sibyl_notify_result(SibylApp* app, SibVerdict verdict);
void sibyl_notify_burst(SibylApp* app);
