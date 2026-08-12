/*
 * The classifier, and above all the honesty rules.
 *
 * Two things are being tested here and only one of them is "does it get the
 * right answer". The other is "does it refuse to overclaim" - that a signal
 * nothing decoded can never be CONFIRMED however cleanly it fingerprints,
 * that naming an encoder chip is not allowed to masquerade as naming a
 * product, and that a ragged capture is scored down rather than dressed up.
 *
 * Those caps are the difference between a teaching tool and a toy, so they
 * are asserted directly instead of being inferred from example outputs.
 */
#include "sib_gen.h"
#include "sib_test.h"

#include "../helpers/sib_classify.h"
#include "../helpers/sib_library.h"

#include <string.h>

/* Build the features for a synthetic capture in one call. */
static void measure(
    SibPulseTrain* t,
    uint8_t repeats,
    uint32_t gap_us,
    SibFeatures* f) {
    SibBurstSummary s[SIB_MAX_BURSTS];
    if(repeats > SIB_MAX_BURSTS) repeats = SIB_MAX_BURSTS;
    sib_repeat_summaries(t, repeats, gap_us, s);
    sib_features_extract(t, s, repeats, f);
}

static void ctx_init(SibContext* c, uint32_t hz, SibMod mod) {
    memset(c, 0, sizeof(*c));
    c->frequency_hz = hz;
    c->mod = mod;
    c->rssi_dbm = -62;
}

static const char* cls_name_of(SibClass c) {
    return sib_class_name(c);
}

/* Where did `cls` land in the ranking? -1 if it did not make the list. */
static int rank_of(const SibResult* r, SibClass cls) {
    for(uint8_t i = 0; i < r->n_cand; i++) {
        if(r->cand[i].cls == cls) return (int)i;
    }
    return -1;
}

static void show(const char* label, const SibResult* r) {
    printf("  %-22s %-12s %3u%%  ", label, sib_verdict_label(r->verdict), r->confidence);
    for(uint8_t i = 0; i < r->n_cand; i++) {
        printf("%s(%u) ", cls_name_of(r->cand[i].cls), r->cand[i].score);
    }
    printf("\n");
}

/* ---- a decoded, product-specific protocol is an identification --------- */
static void test_decoded_specific(void) {
    printf("Decoded product-specific protocol\n");

    SibPulseTrain t;
    sib_gen_pwm(&t, 640, 56, 2, 1, 0, 6, 0x11u);
    SibFeatures f;
    measure(&t, 4, 30000, &f);

    SibContext c;
    ctx_init(&c, 433420000u, SibModOok);
    c.decoded = true;
    c.proto_dynamic = true;
    strcpy(c.protocol, "Somfy Telis");
    c.proto_bits = 56;

    SibResult r;
    sib_classify(&f, &c, &r);
    show("Somfy Telis", &r);

    CHECK(r.verdict == SibVerdictConfirmed, "verdict = %s, want CONFIRMED",
          sib_verdict_label(r.verdict));
    CHECK(r.cand[0].cls == SibClassBlinds, "top = %s, want Blind motor",
          cls_name_of(r.cand[0].cls));
    CHECK(!r.generic_encoder, "Somfy is not a generic encoder");
    CHECK(strlen(r.proto_note) > 0, "protocol note should be populated");

    /* Garage openers are the neighbouring class; a specific decode must push
     * them well down rather than leaving a near-tie. */
    int gate = rank_of(&r, SibClassGateRemote);
    if(gate >= 0) {
        CHECK(r.cand[gate].score < r.cand[0].score - 20,
              "runner-up too close: %u vs %u", r.cand[gate].score, r.cand[0].score);
    }
}

/* ---- naming a chip is not naming a product ----------------------------- */
static void test_generic_encoder(void) {
    printf("Decoded generic encoder chip\n");

    SibPulseTrain t;
    sib_gen_pwm(&t, 350, 24, 3, 1, 31, 5, 0x22u);
    SibFeatures f;
    measure(&t, 8, 11000, &f);

    SibContext c;
    ctx_init(&c, 433920000u, SibModOok);
    c.decoded = true;
    c.proto_static = true;
    strcpy(c.protocol, "Princeton");
    c.proto_bits = 24;

    SibResult r;
    sib_classify(&f, &c, &r);
    show("Princeton", &r);

    CHECK(r.generic_encoder, "Princeton must be flagged as an encoder chip");
    CHECK(r.confidence <= SIB_CAP_GENERIC_ENCODER, "confidence = %u, cap = %u",
          r.confidence, SIB_CAP_GENERIC_ENCODER);
    CHECK(r.verdict != SibVerdictConfirmed, "an encoder chip can never confirm");
    CHECK(r.n_cand >= 2, "an ambiguous decode must offer alternatives, got %u",
          r.n_cand);

    /* A fixed code rules the car out, whatever else it might be. */
    int car = rank_of(&r, SibClassCarFob);
    CHECK(car != 0, "a fixed code must not be ranked as a car fob");
}

/* ---- a fingerprint alone can be useful but never certain --------------- */
static void test_fingerprint_only_tpms(void) {
    printf("Fingerprint only: tyre sensor\n");

    SibPulseTrain t;
    sib_gen_manchester(&t, 52, 72, 8, 0x33u);
    SibFeatures f;
    measure(&t, 2, 55000, &f);

    SibContext c;
    ctx_init(&c, 433920000u, SibModFsk);

    SibResult r;
    sib_classify(&f, &c, &r);
    show("TPMS shape", &r);

    CHECK(r.cand[0].cls == SibClassTpms, "top = %s, want Tyre sensor",
          cls_name_of(r.cand[0].cls));
    CHECK(r.confidence <= SIB_CAP_NO_DECODE, "confidence = %u, cap = %u", r.confidence,
          SIB_CAP_NO_DECODE);
    CHECK(r.verdict != SibVerdictConfirmed, "no decode can ever confirm");
}

static void test_fingerprint_only_gate(void) {
    printf("Fingerprint only: gate remote\n");

    SibPulseTrain t;
    sib_gen_pwm(&t, 350, 24, 3, 1, 31, 5, 0x44u);
    SibFeatures f;
    measure(&t, 6, 11000, &f);

    SibContext c;
    ctx_init(&c, 433920000u, SibModOok);

    SibResult r;
    sib_classify(&f, &c, &r);
    show("OOK handset shape", &r);

    CHECK(r.confidence <= SIB_CAP_NO_DECODE, "confidence = %u", r.confidence);

    /* This is the crowded corner of the band: gate, doorbell, socket and
     * sensor all ship this exact packet. The right behaviour is a shortlist
     * containing them, not a single confident pick. */
    CHECK(r.n_cand >= 3, "expected a shortlist, got %u", r.n_cand);
    bool crowded = rank_of(&r, SibClassGateRemote) >= 0 ||
                   rank_of(&r, SibClassSocket) >= 0 || rank_of(&r, SibClassDoorbell) >= 0;
    CHECK(crowded, "shortlist should contain the 433 OOK handset classes");
}

/* ---- repeat count is what separates a doorbell from a socket ----------- */
static void test_doorbell_vs_socket(void) {
    printf("Repeat count separates doorbell from socket\n");

    SibPulseTrain t;
    sib_gen_pwm(&t, 340, 24, 3, 1, 31, 5, 0x55u);

    SibContext c;
    ctx_init(&c, 433920000u, SibModOok);

    SibFeatures few, many;
    measure(&t, 5, 10000, &few);
    measure(&t, 12, 10000, &many);

    SibResult r_few, r_many;
    sib_classify(&few, &c, &r_few);
    sib_classify(&many, &c, &r_many);
    show("5 copies", &r_few);
    show("12 copies", &r_many);

    int bell_few = rank_of(&r_few, SibClassDoorbell);
    int bell_many = rank_of(&r_many, SibClassDoorbell);
    uint8_t s_few = bell_few >= 0 ? r_few.cand[bell_few].score : 0;
    uint8_t s_many = bell_many >= 0 ? r_many.cand[bell_many].score : 0;
    CHECK(s_many > s_few, "hammering should favour the doorbell: %u -> %u", s_few,
          s_many);
}

/* ---- band and modulation move the answer on their own ------------------ */
static void test_meter_868_fsk(void) {
    printf("868 MHz FSK telemetry frame\n");

    SibPulseTrain t;
    sib_gen_manchester(&t, 60, 300, 6, 0x66u);
    SibFeatures f;
    measure(&t, 1, 0, &f);

    SibContext c;
    ctx_init(&c, 868350000u, SibModFsk);

    SibResult r;
    sib_classify(&f, &c, &r);
    show("868 FSK long frame", &r);

    CHECK(r.cand[0].cls == SibClassMeter, "top = %s, want Meter / telemetry",
          cls_name_of(r.cand[0].cls));
}

static void test_weather_slow_ook(void) {
    printf("433 MHz slow OOK, unprompted\n");

    SibPulseTrain t;
    sib_gen_pwm(&t, 500, 36, 4, 2, 16, 8, 0x77u);
    SibFeatures f;
    measure(&t, 3, 20000, &f);

    SibContext c;
    ctx_init(&c, 433920000u, SibModOok);

    SibResult r;
    sib_classify(&f, &c, &r);
    show("slow OOK", &r);

    CHECK(rank_of(&r, SibClassWeather) >= 0, "weather sensor should be in the list");
}

/* ---- the caps, asserted directly --------------------------------------- */
static void test_caps(void) {
    printf("Honesty caps\n");

    SibContext c;
    ctx_init(&c, 433920000u, SibModOok);

    /* No timing at all: frequency and modulation only. */
    SibFeatures none;
    memset(&none, 0, sizeof(none));
    SibResult r;
    sib_classify(&none, &c, &r);
    show("no timing", &r);
    CHECK(r.confidence <= SIB_CAP_NO_TIMING, "confidence = %u, cap = %u", r.confidence,
          SIB_CAP_NO_TIMING);
    CHECK(r.verdict != SibVerdictLikely && r.verdict != SibVerdictConfirmed,
          "frequency alone cannot reach LIKELY");

    /* Measured, but the pulses did not quantise. */
    SibFeatures ragged;
    memset(&ragged, 0, sizeof(ragged));
    ragged.valid = true;
    ragged.te_us = 350;
    ragged.fit_permille = 420;
    ragged.est_bits = 24;
    ragged.repeats = 6;
    ragged.burst_us = 20000;
    sib_classify(&ragged, &c, &r);
    show("poor fit", &r);
    CHECK(r.confidence <= SIB_CAP_POOR_FIT, "confidence = %u, cap = %u", r.confidence,
          SIB_CAP_POOR_FIT);

    /* Nothing at all. */
    SibContext empty;
    ctx_init(&empty, 0, SibModOok);
    sib_classify(NULL, &empty, &r);
    CHECK(r.verdict == SibVerdictUnknown || r.confidence <= SIB_CAP_NO_TIMING,
          "an empty capture must not produce confidence %u", r.confidence);

    /* Must not crash on NULL output or NULL context. */
    sib_classify(&none, NULL, &r);
    sib_classify(&none, &c, NULL);
}

/* ---- rolling versus fixed reshapes the ranking ------------------------- */
static void test_rolling_bias(void) {
    printf("Rolling code bias\n");

    SibPulseTrain t;
    sib_gen_pwm(&t, 400, 66, 3, 1, 20, 6, 0x88u);
    SibFeatures f;
    measure(&t, 4, 15000, &f);

    SibContext c;
    ctx_init(&c, 433920000u, SibModOok);
    c.decoded = true;
    c.proto_dynamic = true;
    strcpy(c.protocol, "KeeLoq");
    c.proto_bits = 66;

    SibResult r;
    sib_classify(&f, &c, &r);
    show("KeeLoq", &r);

    CHECK(r.generic_encoder, "KeeLoq is a chip, not a product");
    CHECK(r.confidence <= SIB_CAP_GENERIC_ENCODER, "confidence = %u", r.confidence);

    /* A rolling code is incompatible with a doorbell or a weather sensor. */
    int bell = rank_of(&r, SibClassDoorbell);
    int wx = rank_of(&r, SibClassWeather);
    CHECK(bell < 0 || r.cand[bell].score < 30, "rolling code should sink the doorbell");
    CHECK(wx < 0 || r.cand[wx].score < 30, "rolling code should sink the weather sensor");
}

/* ---- everything the UI draws has to fit on the screen ------------------ */
static void test_output_widths(void) {
    printf("Output field widths\n");

    SibPulseTrain t;
    sib_gen_pwm(&t, 350, 24, 3, 1, 31, 5, 0x99u);
    SibFeatures f;
    measure(&t, 8, 11000, &f);

    SibContext c;
    ctx_init(&c, 433920000u, SibModOok);
    c.decoded = true;
    strcpy(c.protocol, "Princeton");

    SibResult r;
    sib_classify(&f, &c, &r);

    for(uint8_t i = 0; i < r.n_reason; i++) {
        size_t len = strlen(r.reason[i]);
        CHECK(len <= 21, "reason %u is %zu chars (\"%s\"), budget is 21", i, len,
              r.reason[i]);
    }
    CHECK(strlen(r.proto_note) < SIB_NOTE_LEN, "note overran its buffer");
    CHECK(r.n_reason >= 3, "expected at least 3 evidence lines, got %u", r.n_reason);
}

/* ---- a sweep, so no input can wedge or overflow the classifier --------- */
static void test_fuzz(void) {
    printf("Parameter sweep\n");

    SibRng rng;
    sib_rng_seed(&rng, 0xF0F0u);

    static const uint32_t freqs[] = {300000000u, 315000000u, 390000000u, 433920000u,
                                     868350000u, 915000000u, 1200000000u};

    int cases = 0;
    for(uint16_t te = 40; te <= 1200; te += 37) {
        for(uint8_t bits = 12; bits <= 120; bits += 17) {
            SibPulseTrain t;
            sib_gen_pwm(&t, te, bits, 3, 1, 31, (uint8_t)(sib_rng_next(&rng) % 15), te);

            SibFeatures f;
            measure(&t, (uint8_t)(1 + sib_rng_next(&rng) % 12),
                    5000 + sib_rng_next(&rng) % 40000, &f);

            SibContext c;
            ctx_init(&c, freqs[sib_rng_next(&rng) % 7],
                     (sib_rng_next(&rng) & 1u) ? SibModFsk : SibModOok);

            SibResult r;
            sib_classify(&f, &c, &r);
            cases++;

            CHECK(r.confidence <= 100, "confidence out of range: %u", r.confidence);
            CHECK(r.n_cand <= SIB_MAX_CANDIDATES, "too many candidates: %u", r.n_cand);
            CHECK(r.n_reason <= SIB_MAX_REASONS, "too many reasons: %u", r.n_reason);
            for(uint8_t i = 0; i < r.n_cand; i++) {
                CHECK(r.cand[i].cls < SibClassCount, "bad class %u", r.cand[i].cls);
                if(i) {
                    CHECK(r.cand[i].score <= r.cand[i - 1].score,
                          "candidates out of order");
                }
            }
            /* The cap that matters most, on every single case. */
            if(!c.decoded) {
                CHECK(r.verdict != SibVerdictConfirmed,
                      "an undecoded signal reached CONFIRMED");
            }
        }
    }
    printf("  %d generated captures\n", cases);
}

int main(void) {
    test_decoded_specific();
    test_generic_encoder();
    test_fingerprint_only_tpms();
    test_fingerprint_only_gate();
    test_doorbell_vs_socket();
    test_meter_868_fsk();
    test_weather_slow_ook();
    test_caps();
    test_rolling_bias();
    test_output_widths();
    test_fuzz();
    return sib_report("classify");
}
