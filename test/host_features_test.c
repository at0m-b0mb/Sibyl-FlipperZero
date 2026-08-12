/*
 * Feature extraction, measured against pulse trains we built ourselves so the
 * right answer is known exactly.
 *
 * The cases that matter most are the ones where a naive estimator goes wrong:
 * a code whose symbol widths are all even multiples (where half the true Te
 * fits just as well), a Manchester stream (where the estimator must not settle
 * on the double-width run), and a jittered capture (where nothing lands on the
 * clock at all).
 */
#include "sib_gen.h"
#include "sib_test.h"

#include "../helpers/sib_features.h"

static void near(const char* name, long got, long want, long tol) {
    long d = got > want ? got - want : want - got;
    CHECK(d <= tol, "%s = %ld, want %ld +/-%ld", name, got, want, tol);
}

/* ---- a clean Princeton-shaped gate remote ------------------------------- */
static void test_pwm_clean(void) {
    printf("PWM, clean, Te=350\n");
    SibPulseTrain t;
    sib_gen_pwm(&t, 350, 24, 3, 1, 31, 0, 0xC0FFEEu);

    SibBurstSummary s[8];
    sib_repeat_summaries(&t, 8, 12000, s);

    SibFeatures f;
    sib_features_extract(&t, s, 8, &f);

    CHECK(f.valid, "features should be valid");
    near("te", f.te_us, 350, 15);
    CHECK(f.fit_permille >= 950, "fit = %u, want >= 950", f.fit_permille);
    CHECK(f.n_widths == 2, "n_widths = %u, want 2", f.n_widths);
    near("est_bits", f.est_bits, 24, 2);
    CHECK(f.repeats == 8, "repeats = %u, want 8", f.repeats);
    CHECK(f.repeat_gap_us == 12000, "gap = %u", f.repeat_gap_us);
    CHECK(!f.manchester_like, "PWM 3:1 must not read as Manchester");
    near("longest_mult", f.longest_mult, 31, 1);
}

/* ---- the same remote heard through a real receiver ---------------------- */
static void test_pwm_jittered(void) {
    printf("PWM, 12%% jitter, Te=350\n");
    SibPulseTrain t;
    sib_gen_pwm(&t, 350, 24, 3, 1, 31, 12, 0x1234u);

    SibBurstSummary s[4];
    sib_repeat_summaries(&t, 4, 9000, s);

    SibFeatures f;
    sib_features_extract(&t, s, 4, &f);

    CHECK(f.valid, "features should be valid");
    near("te", f.te_us, 350, 30);
    CHECK(f.fit_permille >= 800, "fit = %u, want >= 800 under jitter", f.fit_permille);
    CHECK(f.n_widths == 2, "n_widths = %u, want 2", f.n_widths);
}

/* ---- the subharmonic trap ----------------------------------------------
 * Widths of 700 and 2100 are explained perfectly by Te=700 and equally
 * perfectly by Te=350, 175, 87... The estimator has to take the largest, or
 * every bit count it reports is a power of two too big.
 */
static void test_no_subharmonic(void) {
    printf("Subharmonic trap, true Te=700\n");
    SibPulseTrain t;
    sib_gen_pwm(&t, 700, 20, 3, 1, 31, 0, 0xABCDu);

    uint16_t te = sib_estimate_te(t.dur, t.n);
    near("te", te, 700, 30);
    CHECK(te > 500, "te = %u - estimator fell to a subharmonic", te);
}

/* ---- Manchester, as a tyre sensor sends it ----------------------------- */
static void test_manchester(void) {
    printf("Manchester, Te=52\n");
    SibPulseTrain t;
    sib_gen_manchester(&t, 52, 72, 8, 0x5EEDu);

    SibBurstSummary s[2];
    sib_repeat_summaries(&t, 2, 60000, s);

    SibFeatures f;
    sib_features_extract(&t, s, 2, &f);

    CHECK(f.valid, "features should be valid");
    near("te", f.te_us, 52, 6);
    CHECK(f.n_widths == 2, "n_widths = %u, want 2", f.n_widths);
    CHECK(f.longest_mult == 2, "longest_mult = %u, want 2", f.longest_mult);
    CHECK_RANGE(f.duty_permille, 400, 600, "duty");
    CHECK(f.manchester_like, "should read as Manchester");
    CHECK(f.repeats == 2, "repeats = %u, want 2", f.repeats);
}

/* ---- a slow blind-motor symbol rate ------------------------------------ */
static void test_slow_te(void) {
    printf("Slow code, Te=640\n");
    SibPulseTrain t;
    sib_gen_pwm(&t, 640, 40, 2, 1, 0, 6, 0x77u);

    uint16_t te = sib_estimate_te(t.dur, t.n);
    near("te", te, 640, 45);
}

/* ---- noise must not produce a confident answer ------------------------- */
static void test_noise(void) {
    printf("Random noise\n");
    SibPulseTrain t;
    memset(&t, 0, sizeof(t));
    t.first_level = true;

    SibRng rng;
    sib_rng_seed(&rng, 0xDEADBEEFu);
    for(int i = 0; i < 120; i++) {
        sib_push(&t, (uint16_t)(60 + sib_rng_next(&rng) % 1900));
    }

    SibBurstSummary s[1];
    sib_repeat_summaries(&t, 1, 0, s);

    SibFeatures f;
    sib_features_extract(&t, s, 1, &f);

    /* We do not require the estimator to give up - a Te always exists - but a
     * uniform spread of widths must not look like a clocked packet. */
    if(f.valid) {
        CHECK(f.fit_permille < 700, "noise scored fit = %u, want < 700", f.fit_permille);
    }
}

/* ---- the histogram's top bin -------------------------------------------
 * The Te search bins durations up to SIB_TE_SEARCH_MAX_US. A duration one
 * microsecond under the ceiling indexes the very last bin, and if the bin
 * count is computed from the quotient rather than the highest index, that
 * write lands one element past the end - on the heap, inside the radio's
 * worker thread, where it corrupts the capture and not the code that did it.
 */
static void test_histogram_bounds(void) {
    printf("Te histogram bounds\n");

    SibPulseTrain t;
    memset(&t, 0, sizeof(t));
    t.first_level = true;

    /* Sit right on the ceiling, straddle it, and sit right on the floor. */
    for(int i = 0; i < 12; i++) sib_push(&t, SIB_TE_SEARCH_MAX_US - 1);
    for(int i = 0; i < 12; i++) sib_push(&t, SIB_TE_SEARCH_MAX_US);
    for(int i = 0; i < 12; i++) sib_push(&t, SIB_TE_SEARCH_MAX_US + 1);
    for(int i = 0; i < 12; i++) sib_push(&t, SIB_PULSE_MIN_US);
    for(int i = 0; i < 12; i++) sib_push(&t, SIB_PULSE_MIN_US - 1);

    /* No assertion on the value - the point is that this runs clean under
     * ASan. A wrong answer here is a preference; a wrong write is a bug. */
    uint16_t te = sib_estimate_te(t.dur, t.n);
    CHECK(te <= SIB_TE_SEARCH_MAX_US, "te = %u is outside the search range", te);

    /* And the same through the full extractor. */
    SibBurstSummary s[1];
    sib_repeat_summaries(&t, 1, 0, s);
    SibFeatures f;
    sib_features_extract(&t, s, 1, &f);
    CHECK(f.te_us <= SIB_TE_SEARCH_MAX_US, "extracted te out of range");
}

/* ---- degenerate inputs ------------------------------------------------- */
static void test_degenerate(void) {
    printf("Degenerate inputs\n");
    SibFeatures f;

    SibPulseTrain t;
    memset(&t, 0, sizeof(t));
    SibBurstSummary s[1];
    memset(s, 0, sizeof(s));

    sib_features_extract(&t, s, 1, &f);
    CHECK(!f.valid, "empty train must not be valid");

    sib_features_extract(NULL, s, 1, &f);
    CHECK(!f.valid, "NULL train must not be valid");

    sib_features_extract(&t, s, 0, &f);
    CHECK(!f.valid, "zero summaries must not be valid");

    CHECK(sib_estimate_te(NULL, 10) == 0, "NULL durations");
    CHECK(sib_estimate_te(t.dur, 3) == 0, "too few pulses");
    CHECK(sib_measure_fit(t.dur, 0, 350, NULL, NULL) == 0, "empty fit");
    CHECK(sib_measure_fit(t.dur, 10, 0, NULL, NULL) == 0, "zero Te");
}

/* ---- repeat counting --------------------------------------------------- */
static void test_repeats(void) {
    printf("Repeat matching\n");

    SibPulseTrain a, b;
    sib_gen_pwm(&a, 350, 24, 3, 1, 31, 0, 1);
    sib_gen_pwm(&b, 350, 40, 3, 1, 31, 0, 2); /* a different, longer packet */

    SibBurstSummary sa, sb;
    sib_summarise(&a, 0, &sa);
    sib_summarise(&b, 8000, &sb);

    CHECK(sib_bursts_match(&sa, &sa), "a burst matches itself");
    CHECK(!sib_bursts_match(&sa, &sb), "different packets must not match");
    CHECK(!sib_bursts_match(&sa, NULL), "NULL is not a match");

    /* One long packet followed by three copies of a short one: the reference
     * is the first burst, so only it should count. */
    SibBurstSummary mix[4];
    sib_summarise(&b, 0, &mix[0]);
    sib_summarise(&a, 7000, &mix[1]);
    sib_summarise(&a, 7000, &mix[2]);
    sib_summarise(&a, 7000, &mix[3]);

    SibFeatures f;
    sib_features_extract(&b, mix, 4, &f);
    CHECK(f.repeats == 1, "repeats = %u, want 1 (only the reference matched)", f.repeats);
}

/* ---- truncation is reported, not hidden -------------------------------- */
static void test_truncation(void) {
    printf("Buffer overflow flag\n");
    SibPulseTrain t;
    sib_gen_pwm(&t, 200, 400, 3, 1, 31, 0, 9); /* 800 pulses into a 384 slot */

    CHECK(t.truncated, "generator should have hit the ceiling");
    CHECK(t.n == SIB_MAX_PULSES, "n = %u, want %u", t.n, SIB_MAX_PULSES);

    SibBurstSummary s[1];
    sib_repeat_summaries(&t, 1, 0, s);
    SibFeatures f;
    sib_features_extract(&t, s, 1, &f);
    CHECK(f.truncated, "truncation must reach the features struct");
}

int main(void) {
    test_pwm_clean();
    test_pwm_jittered();
    test_no_subharmonic();
    test_manchester();
    test_slow_te();
    test_noise();
    test_histogram_bounds();
    test_degenerate();
    test_repeats();
    test_truncation();
    return sib_report("features");
}
