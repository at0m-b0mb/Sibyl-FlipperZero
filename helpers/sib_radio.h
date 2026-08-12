/**
 * Sibyl - the radio.
 *
 * Brings up the internal CC1101 and does two things with what comes back that
 * a normal receiver does not.
 *
 * First, it runs the full Flipper Sub-GHz decoder stack, so anything the
 * firmware already knows how to read gets named outright.
 *
 * Second - and this is what makes the app work on the signals the firmware
 * does NOT know - it keeps the raw pulse train. Every level transition the
 * demodulator produces is timed and stored, segmented into bursts, and handed
 * to the feature extractor. That is how a tyre sensor or an off-brand weather
 * station still gets identified when no decoder claims it.
 *
 * Keeping raw pulses off the air means keeping noise as well, so bursts pass
 * an acceptance test before they join a session: enough pulses to be a packet,
 * and a symbol quantum that actually explains their widths. Uniform noise
 * fails the second test decisively, which is exactly the property the host
 * tests pin down. The measurement that classifies a signal is the same one
 * that decides it was a signal at all.
 *
 * Strictly listen-only. Sibyl never transmits, replays or clones.
 */
#pragma once

#include "sib_classify.h"
#include "sib_features.h"

#include <furi.h>
#include <furi_hal.h>
#include <gui/view_dispatcher.h>

#ifdef __cplusplus
extern "C" {
#endif

/* A burst has to carry at least this many edges before it is worth measuring.
 * Below it there is no packet short enough to care about and no fit estimate
 * worth trusting. */
#define SIB_MIN_BURST_PULSES 20

/* ...and its pulses have to quantise at least this well. Uniform noise scores
 * far below; a real clocked packet, even a weak one, scores far above. */
#define SIB_MIN_BURST_FIT 700

/* Once a burst has been accepted, this much quiet ends the capture session and
 * the result is analysed. Comfortably longer than the gap any transmitter
 * leaves between repeats of one packet. */
#define SIB_SESSION_IDLE_MS 420

/* Everything one capture session gathered, ready to hand to the classifier. */
typedef struct {
    SibPulseTrain train; /* reference burst, in full             */
    SibBurstSummary burst[SIB_MAX_BURSTS];
    uint8_t n_bursts;

    uint32_t frequency; /* Hz, as tuned                         */
    uint8_t preset; /* FuriHalSubGhzPreset in force         */
    int8_t rssi; /* dBm at the strongest moment          */

    bool decoded; /* a protocol decoder claimed it        */
    char protocol[28];
    bool proto_static;
    bool proto_dynamic;
    uint16_t proto_bits;
    uint64_t proto_fp; /* hash of the decoded parcel           */
} SibSession;

/* Radio band offered in Settings and swept by Find Band. */
typedef struct {
    uint32_t frequency;
    const char* label;
} SibBand;

#define SIB_BAND_COUNT 16
extern const SibBand sib_bands[SIB_BAND_COUNT];

/** Index of 433.92 MHz - the default, and the fallback for an invalid tune. */
#define SIB_BAND_DEFAULT 10

/* Modulation preset offered in Settings. `mod` is what the classifier is told
 * the signal was modulated with, which is not the same thing as the CC1101
 * filter width used to receive it. */
typedef struct {
    const char* label;
    uint8_t preset; /* FuriHalSubGhzPreset             */
    SibMod mod;
} SibModPreset;

#define SIB_MOD_COUNT 4
extern const SibModPreset sib_mods[SIB_MOD_COUNT];

/** One band's result from the Find Band sweep. */
typedef struct {
    int8_t floor_dbm;
    int8_t peak_dbm;
    int8_t last_dbm;
    bool seen;
} SibHuntBand;

/** A band must beat its own noise floor by this much to count as a hit. */
#define SIB_HUNT_MIN_DELTA_DB 10

typedef struct SibRadio SibRadio;

/**
 * @param view_dispatcher  where the worker posts progress
 * @param burst_event      custom-event id posted when a burst is accepted
 */
SibRadio* sib_radio_alloc(ViewDispatcher* view_dispatcher, uint32_t burst_event);
void sib_radio_free(SibRadio* radio);

/** Tune. `preset` is a FuriHalSubGhzPreset, `mod` what to report. */
void sib_radio_configure(SibRadio* radio, uint32_t frequency, uint8_t preset, SibMod mod);

/** Begin / end listening. start() clears the session. */
void sib_radio_start(SibRadio* radio);
void sib_radio_stop(SibRadio* radio);
bool sib_radio_is_running(SibRadio* radio);

/** Throw away whatever has been captured and keep listening. */
void sib_radio_reset_session(SibRadio* radio);

/** Accepted bursts so far. */
uint8_t sib_radio_burst_count(SibRadio* radio);

/** Milliseconds since the last accepted burst. UINT32_MAX if there are none. */
uint32_t sib_radio_idle_ms(SibRadio* radio);

/**
 * True when at least one burst has been accepted and the air has been quiet
 * long enough that the transmission is over. This is the app's cue to analyse.
 */
bool sib_radio_session_ready(SibRadio* radio);

/** Copy the session out for analysis. Returns false if nothing was captured. */
bool sib_radio_snapshot(SibRadio* radio, SibSession* out);

/* ------------------------------------------------------- diagnostics ----- */

/** Current carrier strength in dBm. Valid while listening or hunting. */
float sib_radio_rssi(SibRadio* radio);

/**
 * Raw level transitions seen since start(). Climbing fast means the radio IS
 * hearing something even if nothing was accepted - which is how you tell
 * "wrong frequency" from "this is not a packet I can measure".
 */
uint32_t sib_radio_edges(SibRadio* radio);

/** Bursts seen but rejected by the acceptance test. Honest noise counter. */
uint32_t sib_radio_rejected(SibRadio* radio);

/** True once a protocol decoder has claimed something in this session. */
bool sib_radio_decoded(SibRadio* radio);

/* ---------------------------------------------------------- find band ---- */

/**
 * Sweep every band measuring RSSI. Hold the remote down while this runs: the
 * band it transmits on climbs far above its own noise floor. Mutually
 * exclusive with sib_radio_start().
 */
void sib_radio_hunt_start(SibRadio* radio);
void sib_radio_hunt_stop(SibRadio* radio);
bool sib_radio_hunt_is_running(SibRadio* radio);
uint8_t sib_radio_hunt_snapshot(SibRadio* radio, SibHuntBand* out, uint8_t max);
uint32_t sib_radio_hunt_sweeps(SibRadio* radio);

/** Index into sib_bands of the biggest peak-over-floor delta, or -1 if none
 *  beat SIB_HUNT_MIN_DELTA_DB. Never guesses: no signal, no answer. */
int8_t sib_radio_hunt_best(SibRadio* radio);

#ifdef __cplusplus
}
#endif
