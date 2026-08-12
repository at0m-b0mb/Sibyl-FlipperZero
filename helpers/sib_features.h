/**
 * Sibyl - raw signal feature extraction.
 *
 * Everything in here is pure integer logic over a pulse train: no radio, no
 * GUI, no floats. Give it the list of on/off durations the demodulator handed
 * us and it measures the things that actually distinguish one kind of Sub-GHz
 * device from another:
 *
 *   Te            the symbol quantum - the shortest width the transmitter
 *                 builds every other width out of. A gate remote lives near
 *                 350 us, a TPMS sensor near 50 us. This is the single most
 *                 discriminating number in the whole app.
 *   fit           how cleanly every pulse quantises to a multiple of Te.
 *                 High fit means a real, clocked digital packet. Low fit
 *                 means noise, or a modulation we are not demodulating right.
 *   widths        how many distinct pulse widths the packet uses. Two is a
 *                 binary code (PWM or Manchester); many is either PPM or junk.
 *   repeats       how many times the same packet was retransmitted back to
 *                 back. A doorbell hammers it out a dozen times, a tyre
 *                 sensor sends it once or twice and goes quiet for a minute.
 *   duty          fraction of the burst spent with the carrier on. Manchester
 *                 sits near 50%; PWM codes with a long "0" sit well below.
 *
 * No floats anywhere: every ratio is carried in permille (0..1000) so the
 * numbers a host test sees are bit-for-bit the numbers the Flipper computes,
 * and none of the firmware's -fsingle-precision-constant traps can apply.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* One burst is at most this many level durations. 384 covers a 128-bit PWM
 * packet with sync (2 pulses/bit) with room to spare; anything longer is
 * truncated and flagged rather than silently mis-measured. */
#define SIB_MAX_PULSES 384

/* How many repeats of a packet we keep summaries for in one capture session. */
#define SIB_MAX_BURSTS 12

/* Durations outside this window are not symbols. Below the floor is CC1101
 * envelope noise; above the ceiling is a gap between packets, not a pulse. */
#define SIB_PULSE_MIN_US 30
#define SIB_PULSE_MAX_US 30000

/* A quiet stretch at least this long ends the current burst. Chosen above the
 * longest inter-symbol sync gap in the common OOK remotes (Princeton's is
 * ~31x Te, about 10 ms at Te=350) and below the shortest gap a transmitter
 * leaves between repeats. */
#define SIB_BURST_GAP_US 24000

/* Histogram geometry for the Te search. Public only so that callers can own
 * the scratch it needs; 20 us bins are plenty, because a bin only ever seeds
 * the search and the estimate itself is refined against the exact durations. */
#define SIB_TE_SEARCH_MAX_US 2600
#define SIB_TE_BIN_US        20
/* +1 because the highest eligible duration is SIB_TE_SEARCH_MAX_US - 1, which
 * indexes one bin past the quotient whenever the span does not divide evenly. */
#define SIB_TE_BINS (((SIB_TE_SEARCH_MAX_US - 1 - SIB_PULSE_MIN_US) / SIB_TE_BIN_US) + 1)

/**
 * Working memory for the Te search.
 *
 * The subghz worker thread runs on 2 KB of stack and calls into this code from
 * its pair callback, underneath the protocol decoders. Putting a few hundred
 * bytes of histogram on that stack is how you get a hard fault on a busy band
 * and nowhere near the code that caused it, so anything running in that
 * context owns one of these on the heap and passes it in.
 */
typedef struct {
    uint16_t hist[SIB_TE_BINS];
} SibScratch;

/* Full detail for one burst: the alternating on/off durations, in order. */
typedef struct {
    uint16_t dur[SIB_MAX_PULSES]; /* microseconds, clamped                 */
    uint16_t n; /* how many are valid                    */
    bool first_level; /* level of dur[0] (true = carrier on)   */
    bool truncated; /* ran out of room - n is not the whole  */
} SibPulseTrain;

/* Cheap summary we keep for every burst in the session, used to count repeats
 * without holding twelve full pulse trains in RAM. */
typedef struct {
    uint16_t n_pulses;
    uint32_t total_us; /* burst length, first edge to last      */
    uint32_t high_us; /* time with the carrier on              */
    uint32_t gap_before_us; /* silence before this burst started     */
} SibBurstSummary;

/* Two bursts count as repeats of one packet when their pulse counts agree to
 * within this many pulses AND their lengths agree to within the tolerance
 * below. Both are deliberately loose: the demodulator drops or splits an edge
 * now and then on a weak signal, and a repeat that is 3% short is still a
 * repeat. */
#define SIB_REPEAT_PULSE_SLACK 4
#define SIB_REPEAT_LEN_TOL_PERMILLE 120 /* +/-12% */

/* What we measured. Every field is derived, nothing is assumed. */
typedef struct {
    bool valid; /* false = not enough signal to measure   */

    uint16_t te_us; /* symbol quantum                        */
    uint16_t fit_permille; /* 0..1000, how well pulses fit k*Te     */
    uint8_t n_widths; /* distinct populated widths (in Te)     */
    uint16_t longest_mult; /* longest pulse, in Te units            */

    uint16_t n_pulses; /* pulses in the reference burst         */
    uint32_t burst_us; /* reference burst length                */
    uint16_t duty_permille; /* 0..1000 carrier-on fraction           */
    uint16_t est_bits; /* rough payload size, see note in .c    */

    uint8_t repeats; /* bursts matching the reference (>=1)   */
    uint32_t repeat_gap_us; /* median gap between those repeats      */

    bool manchester_like; /* two widths at 1:2 and duty near half  */
    bool truncated; /* reference burst overflowed the buffer */
} SibFeatures;

/**
 * Measure one capture session.
 *
 * @param train    full pulse train of the reference (first complete) burst
 * @param sums     per-burst summaries, sums[0] describing `train`
 * @param n_sums   how many summaries are valid (>= 1)
 * @param out      filled in; out->valid is false if the burst is unmeasurable
 */
void sib_features_extract(
    const SibPulseTrain* train,
    const SibBurstSummary* sums,
    uint8_t n_sums,
    SibFeatures* out);

/* ------------------------------------------------------------ internals --
 * Exposed only so the host tests can pin the pieces down individually.
 */

/**
 * Estimate the symbol quantum from a set of durations.
 *
 * Seeds from low percentiles of the duration histogram, so sync gaps cannot
 * drag the estimate up, then refines by averaging the real durations clustered
 * around each seed and keeps whichever candidate explains the packet best.
 * Returns 0 when no cluster holds enough of the population to be believable.
 *
 * The plain form puts its scratch on the stack. Anything running on a small
 * stack - the subghz worker in particular - must use the _scratch form and
 * own the buffer itself.
 */
uint16_t sib_estimate_te(const uint16_t* dur, uint16_t n);
uint16_t sib_estimate_te_scratch(const uint16_t* dur, uint16_t n, SibScratch* scratch);

/**
 * How well `dur` quantises to whole multiples of `te`, in permille, plus the
 * number of distinct populated multiples and the largest multiple seen.
 * `n_widths` and `longest_mult` may be NULL.
 */
uint16_t sib_measure_fit(
    const uint16_t* dur,
    uint16_t n,
    uint16_t te,
    uint8_t* n_widths,
    uint16_t* longest_mult);

/** True when two burst summaries look like retransmissions of one packet. */
bool sib_bursts_match(const SibBurstSummary* a, const SibBurstSummary* b);

#ifdef __cplusplus
}
#endif
