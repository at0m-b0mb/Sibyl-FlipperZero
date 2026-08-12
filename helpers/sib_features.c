#include "sib_features.h"

#include <string.h>

/* ---------------------------------------------------------------- Te ----
 *
 * The symbol quantum is the largest duration that explains every other
 * duration in the packet as a whole multiple of itself. "Largest" is the
 * important half: half of a valid Te divides every width just as neatly, so
 * a search that simply takes the best-scoring candidate will happily settle
 * on Te/2 and report a 24-bit code as 48 bits. We therefore score a spread
 * of candidates and, among those that fit equally well, keep the biggest.
 *
 * The candidates come from the duration histogram - a few low percentiles
 * (the fundamental is at the short end, because everything else is a multiple
 * of it) plus the most populated bins (a clean code spends most of its time
 * at one or two widths). Each is then refined by averaging the real durations
 * clustered around it, so the final number is not quantised to a bin.
 */

/* A pulse counts as fitting when it lands within this much of k*Te. */
#define SIB_FIT_TOL_PCT 30

/* Multiples beyond this are gaps between symbol groups, not symbols. They are
 * excluded from the fit score (a 31x sync gap should not be judged against a
 * symbol tolerance) but still reported, because their length is a signature. */
#define SIB_MAX_SYMBOL_MULT 64

/* A width has to hold this share of the packet before we call it a width and
 * not a stray edge. */
#define SIB_WIDTH_MIN_PERMILLE 50

/* The cluster behind a Te estimate must hold this share of the packet. */
#define SIB_TE_CLUSTER_MIN_PERMILLE 100

/* Refinement window around the current estimate, as a percentage. Upper bound
 * stays under 2x so a Manchester packet's double-width symbols cannot pull
 * the estimate up into the gap between the two widths. */
#define SIB_TE_WIN_LO_PCT 60
#define SIB_TE_WIN_HI_PCT 160

static bool sib_is_pulse(uint16_t d) {
    return d >= SIB_PULSE_MIN_US && d <= SIB_PULSE_MAX_US;
}

/* One refinement pass: mean of every duration inside the window around `te`.
 * Returns 0 if the window came up empty. */
static uint16_t sib_refine(const uint16_t* dur, uint16_t n, uint32_t te, uint32_t* count_out) {
    uint32_t lo = te * SIB_TE_WIN_LO_PCT / 100;
    uint32_t hi = te * SIB_TE_WIN_HI_PCT / 100;
    if(lo < SIB_PULSE_MIN_US) lo = SIB_PULSE_MIN_US;

    uint32_t sum = 0, count = 0;
    for(uint16_t i = 0; i < n; i++) {
        uint32_t d = dur[i];
        if(d >= lo && d <= hi) {
            sum += d;
            count++;
        }
    }
    if(count_out) *count_out = count;
    if(count == 0) return 0;
    return (uint16_t)(sum / count);
}

uint16_t sib_measure_fit(
    const uint16_t* dur,
    uint16_t n,
    uint16_t te,
    uint8_t* n_widths,
    uint16_t* longest_mult) {
    if(n_widths) *n_widths = 0;
    if(longest_mult) *longest_mult = 0;
    if(te < SIB_PULSE_MIN_US || n == 0) return 0;

    /* population per multiple, 1..SIB_MAX_SYMBOL_MULT */
    uint16_t pop[SIB_MAX_SYMBOL_MULT + 1];
    memset(pop, 0, sizeof(pop));

    uint32_t considered = 0, good = 0;
    uint16_t longest = 0;

    for(uint16_t i = 0; i < n; i++) {
        uint32_t d = dur[i];
        if(!sib_is_pulse((uint16_t)d)) continue;

        uint32_t m = (d + te / 2) / te;
        if(m > longest && m < 1000) longest = (uint16_t)m;

        /* Shorter than half a symbol: nothing this packet is built from can
         * explain it, so it counts against the estimate rather than being
         * quietly ignored. */
        if(m == 0) {
            considered++;
            continue;
        }
        if(m > SIB_MAX_SYMBOL_MULT) continue; /* an inter-group gap */

        considered++;
        pop[m]++;

        uint32_t ideal = m * te;
        uint32_t err = d > ideal ? d - ideal : ideal - d;
        if(err * 100 <= (uint32_t)te * SIB_FIT_TOL_PCT) good++;
    }

    if(longest_mult) *longest_mult = longest;
    if(considered == 0) return 0;

    if(n_widths) {
        uint32_t floor_pop = considered * SIB_WIDTH_MIN_PERMILLE / 1000;
        if(floor_pop < 2) floor_pop = 2;
        uint8_t widths = 0;
        for(uint8_t m = 1; m <= SIB_MAX_SYMBOL_MULT; m++) {
            if(pop[m] >= floor_pop) widths++;
        }
        *n_widths = widths;
    }

    return (uint16_t)(good * 1000 / considered);
}

uint16_t sib_estimate_te(const uint16_t* dur, uint16_t n) {
    SibScratch scratch;
    return sib_estimate_te_scratch(dur, n, &scratch);
}

uint16_t sib_estimate_te_scratch(const uint16_t* dur, uint16_t n, SibScratch* scratch) {
    if(!dur || !scratch || n < 6) return 0;

    /* Histogram of everything short enough to be a symbol. */
    uint16_t* hist = scratch->hist;
    memset(hist, 0, sizeof(scratch->hist));

    uint32_t eligible = 0;
    for(uint16_t i = 0; i < n; i++) {
        uint16_t d = dur[i];
        if(d < SIB_PULSE_MIN_US || d >= SIB_TE_SEARCH_MAX_US) continue;
        hist[(d - SIB_PULSE_MIN_US) / SIB_TE_BIN_US]++;
        eligible++;
    }
    if(eligible < 6) return 0;

    /* Candidate seeds: low percentiles catch the fundamental, populated bins
     * catch the width the packet actually spends its time at. */
    uint16_t seeds[8];
    uint8_t n_seeds = 0;

    static const uint8_t pct[4] = {3, 10, 25, 50};
    uint8_t pi = 0;
    uint32_t cum = 0;
    for(uint16_t b = 0; b < SIB_TE_BINS && pi < 4; b++) {
        cum += hist[b];
        while(pi < 4 && cum * 100 >= eligible * pct[pi]) {
            uint16_t centre = SIB_PULSE_MIN_US + b * SIB_TE_BIN_US + SIB_TE_BIN_US / 2;
            if(n_seeds < 8) seeds[n_seeds++] = centre;
            pi++;
        }
    }

    /* Three most populated bins. */
    for(uint8_t k = 0; k < 3 && n_seeds < 8; k++) {
        uint16_t best_b = 0, best_v = 0;
        for(uint16_t b = 0; b < SIB_TE_BINS; b++) {
            bool taken = false;
            for(uint8_t s = 0; s < n_seeds; s++) {
                uint16_t c = SIB_PULSE_MIN_US + b * SIB_TE_BIN_US + SIB_TE_BIN_US / 2;
                if(seeds[s] == c) taken = true;
            }
            if(!taken && hist[b] > best_v) {
                best_v = hist[b];
                best_b = b;
            }
        }
        if(best_v == 0) break;
        seeds[n_seeds++] = SIB_PULSE_MIN_US + best_b * SIB_TE_BIN_US + SIB_TE_BIN_US / 2;
    }

    /* Refine each seed, then keep the best fit. Ties go to the LARGEST Te:
     * every subharmonic of the true quantum fits just as well, and only the
     * largest of them is the real symbol width. */
    uint16_t best_te = 0;
    uint16_t best_fit = 0;

    for(uint8_t s = 0; s < n_seeds; s++) {
        uint32_t te = seeds[s];
        uint32_t cluster = 0;
        for(uint8_t it = 0; it < 3; it++) {
            uint16_t next = sib_refine(dur, n, te, &cluster);
            if(next == 0) {
                te = 0;
                break;
            }
            if(next == te) break;
            te = next;
        }
        if(te < SIB_PULSE_MIN_US) continue;
        if(cluster * 1000 < eligible * SIB_TE_CLUSTER_MIN_PERMILLE) continue;

        uint16_t fit = sib_measure_fit(dur, n, (uint16_t)te, NULL, NULL);

        /* 2% of fit is inside the noise of a real capture, so treat those as
         * equal and let size decide. */
        bool better = fit > best_fit + 20;
        bool tied = !better && (fit + 20 >= best_fit) && (te > best_te);
        if(best_te == 0 || better || tied) {
            best_fit = fit > best_fit ? fit : best_fit;
            best_te = (uint16_t)te;
        }
    }

    return best_te;
}

/* ------------------------------------------------------------ repeats --- */

bool sib_bursts_match(const SibBurstSummary* a, const SibBurstSummary* b) {
    if(!a || !b) return false;
    if(a->n_pulses == 0 || b->n_pulses == 0) return false;

    uint16_t dp = a->n_pulses > b->n_pulses ? a->n_pulses - b->n_pulses :
                                              b->n_pulses - a->n_pulses;
    if(dp > SIB_REPEAT_PULSE_SLACK) return false;

    uint32_t big = a->total_us > b->total_us ? a->total_us : b->total_us;
    uint32_t small = a->total_us > b->total_us ? b->total_us : a->total_us;
    if(big == 0) return false;
    uint32_t diff = big - small;
    return diff * 1000 <= big * SIB_REPEAT_LEN_TOL_PERMILLE;
}

static uint32_t sib_median_u32(uint32_t* v, uint8_t n) {
    if(n == 0) return 0;
    for(uint8_t i = 1; i < n; i++) {
        uint32_t key = v[i];
        int8_t j = (int8_t)i - 1;
        while(j >= 0 && v[j] > key) {
            v[j + 1] = v[j];
            j--;
        }
        v[j + 1] = key;
    }
    return v[n / 2];
}

/* ------------------------------------------------------------ extract --- */

void sib_features_extract(
    const SibPulseTrain* train,
    const SibBurstSummary* sums,
    uint8_t n_sums,
    SibFeatures* out) {
    if(!out) return;
    memset(out, 0, sizeof(*out));
    if(!train || !sums || n_sums == 0) return;

    out->truncated = train->truncated;
    out->n_pulses = train->n;
    out->burst_us = sums[0].total_us;

    if(sums[0].total_us > 0) {
        out->duty_permille = (uint16_t)((uint64_t)sums[0].high_us * 1000 / sums[0].total_us);
        if(out->duty_permille > 1000) out->duty_permille = 1000;
    }

    /* Repeats: how many of this session's bursts look like the same packet,
     * and how far apart they were sent. The reference burst counts itself. */
    uint8_t repeats = 0;
    uint32_t gaps[SIB_MAX_BURSTS];
    uint8_t n_gaps = 0;
    for(uint8_t i = 0; i < n_sums && i < SIB_MAX_BURSTS; i++) {
        if(sib_bursts_match(&sums[0], &sums[i])) {
            repeats++;
            if(i > 0 && n_gaps < SIB_MAX_BURSTS) gaps[n_gaps++] = sums[i].gap_before_us;
        }
    }
    out->repeats = repeats ? repeats : 1;
    out->repeat_gap_us = sib_median_u32(gaps, n_gaps);

    /* Timing. Everything below needs a symbol quantum to mean anything, so a
     * failed estimate leaves the whole measurement invalid rather than
     * producing confident-looking nonsense. */
    uint16_t te = sib_estimate_te(train->dur, train->n);
    if(te == 0) return;

    out->te_us = te;
    out->fit_permille = sib_measure_fit(
        train->dur, train->n, te, &out->n_widths, &out->longest_mult);

    /* Payload estimate. Every binary Sub-GHz line code in this space - PWM,
     * PPM, Manchester - spends one carrier-on and one carrier-off period per
     * bit, so symbol pulses divided by two is the bit count to within the
     * preamble. It is labelled an estimate in the UI for exactly that reason. */
    uint16_t symbol_pulses = 0;
    for(uint16_t i = 0; i < train->n; i++) {
        uint32_t d = train->dur[i];
        if(!sib_is_pulse((uint16_t)d)) continue;
        uint32_t m = (d + te / 2) / te;
        if(m >= 1 && m <= SIB_MAX_SYMBOL_MULT) symbol_pulses++;
    }
    out->est_bits = symbol_pulses / 2;

    /* Manchester leaves the carrier on for half the packet and uses exactly
     * two widths at 1:2. PWM codes with a long zero sit far below half. */
    out->manchester_like = (out->n_widths == 2) && (out->longest_mult >= 2) &&
                           (out->duty_permille >= 400) && (out->duty_permille <= 600);

    out->valid = true;
}
