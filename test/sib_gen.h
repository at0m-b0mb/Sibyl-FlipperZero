/*
 * Synthetic signal generator for the host tests.
 *
 * Builds pulse trains shaped like the real thing: PWM codes the way a
 * Princeton-family encoder emits them, Manchester the way a tyre sensor does,
 * plus a jitter knob so the tests exercise the estimator against edges that
 * are not perfectly on the clock - which is the only kind a real radio ever
 * hands you.
 *
 * Deterministic on purpose: the PRNG is seeded per case, so a failure is
 * reproducible rather than "it went red once on CI".
 */
#pragma once

#include "../helpers/sib_features.h"

#include <string.h>

typedef struct {
    uint32_t state;
} SibRng;

static inline void sib_rng_seed(SibRng* r, uint32_t seed) {
    r->state = seed ? seed : 1;
}

/* xorshift32 - small, fast, and identical on every platform CI might use. */
static inline uint32_t sib_rng_next(SibRng* r) {
    uint32_t x = r->state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    r->state = x;
    return x;
}

/* Apply +/- jitter_pct to a duration. */
static inline uint16_t sib_jitter(SibRng* r, uint32_t d, uint8_t jitter_pct) {
    if(jitter_pct == 0) return (uint16_t)d;
    uint32_t span = d * jitter_pct / 100;
    if(span == 0) return (uint16_t)d;
    int32_t off = (int32_t)(sib_rng_next(r) % (2 * span + 1)) - (int32_t)span;
    int32_t v = (int32_t)d + off;
    if(v < 1) v = 1;
    if(v > 65535) v = 65535;
    return (uint16_t)v;
}

static inline void sib_push(SibPulseTrain* t, uint16_t d) {
    if(t->n >= SIB_MAX_PULSES) {
        t->truncated = true;
        return;
    }
    t->dur[t->n++] = d;
}

/*
 * PWM code, Princeton/EV1527 shaped: every bit is one carrier-on period and
 * one carrier-off period, and the bit's value is which of the two is long.
 * The packet ends with a long silent sync gap.
 *
 * hi_mult/lo_mult are the multiples of Te used for a '1'; a '0' swaps them.
 */
static inline void sib_gen_pwm(
    SibPulseTrain* t,
    uint16_t te,
    uint16_t n_bits,
    uint8_t hi_mult,
    uint8_t lo_mult,
    uint8_t sync_mult,
    uint8_t jitter_pct,
    uint32_t seed) {
    memset(t, 0, sizeof(*t));
    t->first_level = true;

    SibRng rng;
    sib_rng_seed(&rng, seed);

    for(uint16_t i = 0; i < n_bits; i++) {
        bool one = (sib_rng_next(&rng) & 1u) != 0;
        uint8_t h = one ? hi_mult : lo_mult;
        uint8_t l = one ? lo_mult : hi_mult;
        sib_push(t, sib_jitter(&rng, (uint32_t)te * h, jitter_pct));
        sib_push(t, sib_jitter(&rng, (uint32_t)te * l, jitter_pct));
    }
    if(sync_mult) sib_push(t, sib_jitter(&rng, (uint32_t)te * sync_mult, jitter_pct));
}

/*
 * Manchester: each bit is a transition in the middle of its slot, so the run
 * lengths that come out of the demodulator are one or two half-slots. This is
 * what a TPMS sensor and most FSK telemetry looks like.
 */
static inline void sib_gen_manchester(
    SibPulseTrain* t,
    uint16_t te,
    uint16_t n_bits,
    uint8_t jitter_pct,
    uint32_t seed) {
    memset(t, 0, sizeof(*t));

    SibRng rng;
    sib_rng_seed(&rng, seed);

    /* Build the half-symbol level sequence, then run-length encode it. */
    bool level[1024];
    uint16_t n = 0;
    for(uint16_t i = 0; i < n_bits && n + 2 <= 1024; i++) {
        bool one = (sib_rng_next(&rng) & 1u) != 0;
        level[n++] = one;
        level[n++] = !one;
    }
    if(n == 0) return;

    t->first_level = level[0];
    uint16_t run = 1;
    for(uint16_t i = 1; i < n; i++) {
        if(level[i] == level[i - 1]) {
            run++;
        } else {
            sib_push(t, sib_jitter(&rng, (uint32_t)te * run, jitter_pct));
            run = 1;
        }
    }
    sib_push(t, sib_jitter(&rng, (uint32_t)te * run, jitter_pct));
}

/* Sum a train into the summary the extractor expects, using first_level to
 * work out which durations were carrier-on. */
static inline void sib_summarise(
    const SibPulseTrain* t,
    uint32_t gap_before_us,
    SibBurstSummary* s) {
    memset(s, 0, sizeof(*s));
    s->n_pulses = t->n;
    s->gap_before_us = gap_before_us;

    bool level = t->first_level;
    for(uint16_t i = 0; i < t->n; i++) {
        s->total_us += t->dur[i];
        if(level) s->high_us += t->dur[i];
        level = !level;
    }
}

/* Fill `n` summaries that all look like repeats of `t`, spaced `gap` apart. */
static inline void sib_repeat_summaries(
    const SibPulseTrain* t,
    uint8_t n,
    uint32_t gap,
    SibBurstSummary* out) {
    for(uint8_t i = 0; i < n; i++) {
        sib_summarise(t, i == 0 ? 0 : gap, &out[i]);
    }
}
