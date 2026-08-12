#include "sib_classify.h"
#include "sib_library.h"

#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------- bands ----- */

#define SIB_B300 (1u << 0) /* 290-325  MHz: 303.87 / 310 / 315 / 318   */
#define SIB_B330 (1u << 1) /* 326-400  MHz: 330 / 345 / 390            */
#define SIB_B433 (1u << 2) /* 400-440  MHz: the 433 ISM band           */
#define SIB_B868 (1u << 3) /* 860-875  MHz: EU SRD                     */
#define SIB_B915 (1u << 4) /* 900-930  MHz: US ISM                     */
#define SIB_BXXX (1u << 5) /* anything else                            */

static uint8_t sib_band_of(uint32_t hz) {
    if(hz >= 290000000u && hz <= 325000000u) return SIB_B300;
    if(hz > 325000000u && hz <= 400000000u) return SIB_B330;
    if(hz > 400000000u && hz <= 440000000u) return SIB_B433;
    if(hz >= 860000000u && hz <= 875000000u) return SIB_B868;
    if(hz >= 900000000u && hz <= 930000000u) return SIB_B915;
    return SIB_BXXX;
}

#define SIB_MOOK (1u << 0)
#define SIB_MFSK (1u << 1)

/* ------------------------------------------------------- the spec table --
 *
 * One row per device class, holding the envelope of what that class looks
 * like on the air. The numbers are deliberately generous: these are ranges
 * that separate classes from each other, not tolerances that validate a
 * particular product. Where two classes genuinely overlap - a doorbell and a
 * mains socket share both their chip and their band - the ranges overlap too,
 * and the app shows both rather than pretending the overlap is not there.
 *
 * manchester: 1 expect it, 0 expect not, -1 no opinion.
 */
typedef struct {
    uint8_t bands;
    uint8_t mods;
    uint16_t te_lo, te_hi; /* microseconds            */
    uint16_t bits_lo, bits_hi; /* estimated payload bits  */
    uint8_t rep_lo, rep_hi; /* copies per transmission */
    uint32_t burst_lo_us, burst_hi_us; /* one copy's length       */
    int8_t manchester;
} SibSpec;

static const SibSpec sib_specs[SibClassCount] = {
    [SibClassUnknown] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -1},

    /* Cheap OOK handset, generous symbol width, several copies per press. */
    [SibClassGateRemote] =
        {SIB_B300 | SIB_B330 | SIB_B433 | SIB_B868, SIB_MOOK, 200, 800, 12, 80, 3, 20,
         4000, 70000, 0},

    /* Longer packet than a gate remote - serial, button and counter - and
     * frequently FSK, which a gate remote almost never is. */
    [SibClassCarFob] =
        {SIB_B300 | SIB_B433 | SIB_B868, SIB_MOOK | SIB_MFSK, 100, 550, 50, 220, 2, 10,
         15000, 250000, -1},

    /* The outlier of the whole table: microsecond-scale symbols in a burst
     * measured in single-digit milliseconds, then a minute of silence. */
    [SibClassTpms] =
        {SIB_B300 | SIB_B433, SIB_MFSK | SIB_MOOK, 30, 130, 40, 180, 1, 6, 1000, 25000,
         1},

    /* Slow, chatty, unprompted. Sends on a timer whether you ask or not. */
    [SibClassWeather] =
        {SIB_B433 | SIB_B868 | SIB_B915, SIB_MOOK, 200, 1300, 24, 90, 2, 8, 15000,
         140000, 0},

    /* Same silicon as a socket remote; told apart by how hard it repeats. */
    [SibClassDoorbell] =
        {SIB_B300 | SIB_B433, SIB_MOOK, 180, 650, 16, 60, 6, 40, 4000, 45000, 0},

    /* The 24-bit EV1527 workhorse. */
    [SibClassSocket] = {SIB_B433, SIB_MOOK, 150, 650, 18, 36, 3, 12, 6000, 45000, 0},

    /* Event driven: one or two copies, then nothing until something moves. */
    [SibClassSensor] =
        {SIB_B433 | SIB_B868, SIB_MOOK, 150, 650, 20, 80, 1, 6, 6000, 65000, 0},

    /* Slow symbols and a long wake-up preamble for a sleeping motor. */
    [SibClassBlinds] =
        {SIB_B433 | SIB_B868, SIB_MOOK, 320, 1000, 40, 130, 2, 8, 25000, 170000, 0},

    /* Big FSK frames, high bands, sent on a schedule of minutes. */
    [SibClassMeter] =
        {SIB_B868 | SIB_B915, SIB_MFSK, 30, 160, 100, 600, 1, 3, 4000, 120000, -1},

    /* Transmits for as long as the operator holds the control down. */
    [SibClassIndustrial] =
        {SIB_B433 | SIB_B868, SIB_MOOK, 200, 900, 24, 110, 8, 60, 8000, 90000, 0},
};

/* Weights, summing to 100 so a raw score is already a percentage. Timing
 * carries the most because it is the measurement least shared between
 * classes; band and modulation are strong but coarse. */
#define SIB_W_BAND 22
#define SIB_W_MOD 16
#define SIB_W_TE 26
#define SIB_W_BITS 12
#define SIB_W_REP 12
#define SIB_W_BURST 12

/* Score a value against a range: full weight inside, tapering to zero over a
 * margin outside, so a signal that just misses is ranked above one that misses
 * by a mile instead of both landing on zero.
 *
 * The margin is relative to the edge that was missed, not to the width of the
 * range. Every quantity here - symbol width, packet length, bit count - is
 * something you reason about in ratios: 50 us is not "a bit under" 100 us, it
 * is half of it, and a class whose floor is 100 us should score it near zero.
 * An absolute margin taken from the range width makes wide ranges forgiving of
 * everything, which is how a generously specified class ends up second-placed
 * against every signal in the band. */
static uint32_t sib_range_score(uint32_t v, uint32_t lo, uint32_t hi, uint32_t w) {
    if(hi < lo) return 0;
    if(v >= lo && v <= hi) return w;

    uint32_t edge = (v < lo) ? lo : hi;
    uint32_t margin = edge / 2;
    if(margin == 0) return 0;

    uint32_t d = (v < lo) ? (lo - v) : (v - hi);
    if(d >= margin) return 0;
    return w * (margin - d) / margin;
}

/* Bring a raw fingerprint score under a ceiling without destroying the
 * ranking underneath it.
 *
 * Clipping at the cap is the obvious implementation and it is wrong: as soon
 * as two classes both score above the ceiling they land on exactly the same
 * number, the ordering collapses into whatever the loop visited first, and the
 * app confidently announces a tyre sensor is a car key. So the curve is linear
 * up to three quarters of the cap and compresses everything above that into
 * the remaining quarter. Order and relative distance survive, low scores are
 * left alone, and nothing can reach the ceiling but a perfect match. */
static uint32_t sib_apply_cap(uint32_t raw, uint32_t cap) {
    if(raw > 100) raw = 100;
    if(cap >= 100) return raw;

    uint32_t knee = cap * 3 / 4;
    if(raw <= knee) return raw;
    return knee + (raw - knee) * (cap - knee) / (100 - knee);
}

/* --------------------------------------------------------- the ranking --- */

static uint32_t sib_score_class(SibClass cls, const SibFeatures* f, const SibContext* ctx) {
    const SibSpec* s = &sib_specs[cls];
    uint8_t band = sib_band_of(ctx->frequency_hz);
    uint8_t mod = (ctx->mod == SibModFsk) ? SIB_MFSK : SIB_MOOK;

    uint32_t score = 0;
    if(s->bands & band) score += SIB_W_BAND;
    if(s->mods & mod) score += SIB_W_MOD;

    if(!f || !f->valid) return score;

    score += sib_range_score(f->te_us, s->te_lo, s->te_hi, SIB_W_TE);
    score += sib_range_score(f->est_bits, s->bits_lo, s->bits_hi, SIB_W_BITS);
    score += sib_range_score(f->repeats, s->rep_lo, s->rep_hi, SIB_W_REP);
    score += sib_range_score(f->burst_us, s->burst_lo_us, s->burst_hi_us, SIB_W_BURST);

    /* Manchester coding is a strong hint on its own - it is what nearly every
     * tyre sensor uses and what nearly no cheap OOK handset uses. */
    if(s->manchester == 1 && f->manchester_like) {
        score += 8;
    } else if(s->manchester == 0 && f->manchester_like) {
        score = score > 8 ? score - 8 : 0;
    }

    return score;
}

static const char* sib_mod_label(SibMod m) {
    return m == SibModFsk ? "FM (FSK)" : "AM (OOK)";
}

static void sib_add_reason(SibResult* out, const char* text) {
    if(out->n_reason >= SIB_MAX_REASONS || !text) return;
    strncpy(out->reason[out->n_reason], text, SIB_REASON_LEN - 1);
    out->reason[out->n_reason][SIB_REASON_LEN - 1] = '\0';
    out->n_reason++;
}

void sib_classify(const SibFeatures* f, const SibContext* ctx, SibResult* out) {
    if(!out || !ctx) return;
    memset(out, 0, sizeof(*out));

    /* ---- what, if anything, the decoder told us ---- */
    SibProtoInfo proto;
    bool have_proto = ctx->decoded && sib_protocol_lookup(ctx->protocol, &proto);
    if(!have_proto) memset(&proto, 0, sizeof(proto));
    out->generic_encoder = have_proto && !proto.device_specific;

    /* ---- raw fingerprint score for every class ---- */
    uint32_t raw[SibClassCount];
    raw[SibClassUnknown] = 0;
    for(uint8_t c = 1; c < SibClassCount; c++) {
        raw[c] = sib_score_class((SibClass)c, f, ctx);
    }

    /* Rolling versus fixed is decisive for a couple of classes, and it is
     * decided by the decoder rather than by the fingerprint, so it applies to
     * the raw score before any ceiling does. No car has shipped a fixed-code
     * remote in thirty years, and nothing that reports a temperature or rings
     * a chime has ever bothered with a counter. */
    if(ctx->proto_dynamic) {
        raw[SibClassCarFob] += 8;
        raw[SibClassGateRemote] += 4;
        raw[SibClassBlinds] += 4;
        for(uint8_t c = 1; c < SibClassCount; c++) {
            if(c == SibClassTpms || c == SibClassWeather || c == SibClassDoorbell ||
               c == SibClassSocket) {
                raw[c] = raw[c] > 25 ? raw[c] - 25 : 0;
            }
        }
    } else if(ctx->proto_static) {
        raw[SibClassCarFob] = raw[SibClassCarFob] > 25 ? raw[SibClassCarFob] - 25 : 0;
    }

    /* ---- honesty ceilings ----
     * These are the whole point of the app. A fingerprint is inference and can
     * never be promoted to a confirmation, however neat it looks; only a
     * protocol that names one product family gets near the top of the scale. */
    uint32_t cap;
    if(ctx->decoded) {
        cap = SIB_CAP_GENERIC_ENCODER;
    } else if(!f || !f->valid) {
        cap = SIB_CAP_NO_TIMING;
    } else if(f->fit_permille < 600) {
        cap = SIB_CAP_POOR_FIT;
    } else {
        cap = SIB_CAP_NO_DECODE;
    }

    for(uint8_t c = 1; c < SibClassCount; c++) {
        bool in_bias = have_proto && (proto.bias_mask & SIB_BIT(c)) != 0;
        bool is_the_one = have_proto && proto.device_specific && proto.cls == c;

        if(is_the_one) {
            /* A product-specific protocol IS the identification. The
             * fingerprint is demoted to a corroborating detail, worth the last
             * twelve points and nothing more. */
            raw[c] = 86 + (raw[c] > 100 ? 100 : raw[c]) * 12 / 100;
            continue;
        }

        raw[c] = sib_apply_cap(raw[c], cap);

        if(!have_proto) continue;

        if(in_bias) {
            /* Plausible for this protocol: lift a fifth of the way to the
             * ceiling, which cannot overshoot it however often it is applied. */
            raw[c] += (cap - raw[c]) * 20 / 100;
        } else {
            /* Named protocol, and this class is not one of the things it gets
             * fitted to. Push it down without erasing it - the library is a
             * shortlist of what we know, not a census of what exists. */
            raw[c] = raw[c] * (proto.device_specific ? 45u : 75u) / 100u;
        }
    }

    /* ---- rank ---- */
    bool used[SibClassCount];
    memset(used, 0, sizeof(used));
    for(uint8_t slot = 0; slot < SIB_MAX_CANDIDATES; slot++) {
        uint8_t best = 0;
        uint32_t best_score = 0;
        for(uint8_t c = 1; c < SibClassCount; c++) {
            if(used[c]) continue;
            if(raw[c] > best_score) {
                best_score = raw[c];
                best = c;
            }
        }
        if(best == 0 || best_score < 10) break;
        used[best] = true;
        out->cand[slot].cls = (SibClass)best;
        out->cand[slot].score = (uint8_t)(best_score > 100 ? 100 : best_score);
        out->n_cand++;
    }

    out->confidence = out->n_cand ? out->cand[0].score : 0;

    if(out->confidence >= SIB_THRESH_CONFIRMED) {
        out->verdict = SibVerdictConfirmed;
    } else if(out->confidence >= SIB_THRESH_LIKELY) {
        out->verdict = SibVerdictLikely;
    } else if(out->confidence >= SIB_THRESH_POSSIBLE) {
        out->verdict = SibVerdictPossible;
    } else {
        out->verdict = SibVerdictUnknown;
    }

    /* ---- evidence lines ---- */
    char buf[SIB_REASON_LEN];

    if(have_proto) {
        strncpy(out->proto_note, proto.note, SIB_NOTE_LEN - 1);
        sib_add_reason(
            out, proto.device_specific ? "Protocol decoded" : "Encoder chip only");
    } else if(ctx->decoded) {
        strncpy(out->proto_note, ctx->protocol, SIB_NOTE_LEN - 1);
        sib_add_reason(out, "Decoded, not in library");
    } else {
        sib_add_reason(out, "No decode: shape only");
    }

    /* Frequency is clamped before formatting so the compiler can prove the
     * width of every field and -Werror=format-truncation stays quiet. */
    uint32_t mhz = ctx->frequency_hz / 1000000u;
    uint32_t frac = (ctx->frequency_hz % 1000000u) / 10000u;
    if(mhz > 999) mhz = 999;
    if(frac > 99) frac = 99;
    snprintf(buf, sizeof(buf), "%lu.%02lu MHz  %s", (unsigned long)mhz,
             (unsigned long)frac, sib_mod_label(ctx->mod));
    sib_add_reason(out, buf);

    if(f && f->valid) {
        uint32_t te = f->te_us;
        uint32_t fit = f->fit_permille / 10;
        if(te > 9999) te = 9999;
        if(fit > 100) fit = 100;
        snprintf(buf, sizeof(buf), "Te %lu us   fit %lu%%", (unsigned long)te,
                 (unsigned long)fit);
        sib_add_reason(out, buf);

        uint32_t bits = ctx->proto_bits ? ctx->proto_bits : f->est_bits;
        uint32_t reps = f->repeats;
        if(bits > 999) bits = 999;
        if(reps > 99) reps = 99;
        snprintf(buf, sizeof(buf), "%lu bits%s   x%lu", (unsigned long)bits,
                 ctx->proto_bits ? "" : " est", (unsigned long)reps);
        sib_add_reason(out, buf);
    } else {
        sib_add_reason(out, "Timing unmeasurable");
    }
}

const char* sib_verdict_label(SibVerdict v) {
    switch(v) {
    case SibVerdictConfirmed:
        return "CONFIRMED";
    case SibVerdictLikely:
        return "LIKELY";
    case SibVerdictPossible:
        return "POSSIBLE";
    default:
        return "UNIDENTIFIED";
    }
}
