/**
 * Sibyl - the classifier.
 *
 * Turns a measured signal into a ranked guess at what kind of device sent it,
 * and - the part that matters - into an honest statement of how much that
 * guess is worth.
 *
 * There are two completely different kinds of answer in this app and they must
 * never be confused with one another:
 *
 *   DECODED. The Flipper's own protocol stack recognised the packet and named
 *   it. That is a fact about the bits, not a guess. Some of those names map
 *   onto exactly one kind of product - a Somfy Telis packet comes out of a
 *   blind or awning remote and nothing else - and those are the only results
 *   Sibyl will ever call CONFIRMED.
 *
 *   FINGERPRINTED. Nothing decoded, so all we have is the shape of the signal:
 *   which band, which modulation, the symbol width, how long the packet was,
 *   how many times it repeated. That narrows the field a lot, and it is
 *   genuinely useful, but it is inference. It is capped below CONFIRMED and
 *   always shown as a ranked list rather than a single answer.
 *
 * The third case is the one most tools get wrong, so it gets its own flag: a
 * packet can decode perfectly and still not identify the device. Princeton,
 * EV1527, Holtek and friends are *encoder chips*. They are sold by the reel
 * and turn up in gate remotes, doorbells, mains sockets, PIR sensors and
 * garden lights alike. Naming the chip is not naming the product, and Sibyl
 * says so out loud instead of picking whichever one sounds best.
 *
 * Pure logic - no radio, no GUI, no floats.
 */
#pragma once

#include "sib_features.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The kinds of thing that live in the Sub-GHz bands. Deliberately coarse:
 * these are the distinctions a signal's shape can actually support. */
typedef enum {
    SibClassUnknown = 0,
    SibClassGateRemote, /* garage door / gate / barrier handset  */
    SibClassCarFob, /* car key, car alarm handset            */
    SibClassTpms, /* tyre pressure sensor                  */
    SibClassWeather, /* outdoor temp / rain / wind sensor     */
    SibClassDoorbell, /* wireless doorbell push                */
    SibClassSocket, /* remote mains socket, RF light switch  */
    SibClassSensor, /* PIR, door contact, smoke, alarm bits  */
    SibClassBlinds, /* blind / shutter / awning motor        */
    SibClassMeter, /* utility meter, telemetry              */
    SibClassIndustrial, /* crane, hoist, site machinery          */
    SibClassCount
} SibClass;

typedef enum {
    SibModOok = 0, /* on-off keyed - carrier chopped        */
    SibModFsk, /* frequency shift keyed                 */
} SibMod;

/* What the radio knew at the moment of capture, plus whatever the SDK's
 * decoder stack made of the packet. */
typedef struct {
    uint32_t frequency_hz;
    SibMod mod;
    int8_t rssi_dbm;

    bool decoded; /* a protocol decoder claimed the packet */
    char protocol[28]; /* its name, as the SDK reports it       */
    bool proto_dynamic; /* SDK says rolling code                 */
    bool proto_static; /* SDK says fixed code                   */
    uint16_t proto_bits; /* decoded length, 0 if unreported       */
} SibContext;

typedef enum {
    SibVerdictUnknown = 0, /* not enough to say anything            */
    SibVerdictPossible, /* consistent with, little more          */
    SibVerdictLikely, /* the evidence points one way           */
    SibVerdictConfirmed, /* decoded, and the protocol is specific */
} SibVerdict;

typedef struct {
    SibClass cls;
    uint8_t score; /* 0..100 after honesty caps             */
} SibCandidate;

#define SIB_MAX_CANDIDATES 4
#define SIB_MAX_REASONS 4

/* Reasons are drawn straight onto a 128 px line in FontSecondary, so they are
 * budgeted at 21 visible characters and never wrapped. The protocol note is
 * longer and prose-shaped, so it gets its own field and the view wraps it. */
#define SIB_REASON_LEN 24
#define SIB_NOTE_LEN 40

typedef struct {
    SibVerdict verdict;
    uint8_t confidence; /* score of cand[0]                      */

    SibCandidate cand[SIB_MAX_CANDIDATES];
    uint8_t n_cand;

    /* True when a protocol decoded but names an encoder chip rather than a
     * product. The UI must show the runners-up when this is set. */
    bool generic_encoder;

    /* The library's line about the decoded protocol, empty if nothing
     * decoded. Wrapped by the caller. */
    char proto_note[SIB_NOTE_LEN];

    /* Evidence, one short line each, ready to draw. */
    char reason[SIB_MAX_REASONS][SIB_REASON_LEN];
    uint8_t n_reason;
} SibResult;

/**
 * Rank the device classes against one measured signal.
 *
 * @param f    measured features; f->valid == false is handled (the frequency
 *             and any decode still contribute, the timing does not)
 * @param ctx  radio state and decoder outcome at capture time
 * @param out  ranked result
 */
void sib_classify(const SibFeatures* f, const SibContext* ctx, SibResult* out);

/** Human label for a verdict band ("CONFIRMED", "LIKELY", ...). */
const char* sib_verdict_label(SibVerdict v);

/* Score caps. Exposed so the tests can assert the honesty rules directly
 * rather than reimplementing them. */
#define SIB_CAP_NO_DECODE 70 /* fingerprint alone never confirms      */
#define SIB_CAP_GENERIC_ENCODER 74 /* chip named, product not               */
#define SIB_CAP_POOR_FIT 45 /* timing did not quantise cleanly       */
#define SIB_CAP_NO_TIMING 40 /* frequency and nothing else            */

/* Verdict thresholds. */
#define SIB_THRESH_CONFIRMED 85
#define SIB_THRESH_LIKELY 60
#define SIB_THRESH_POSSIBLE 35

#ifdef __cplusplus
}
#endif
