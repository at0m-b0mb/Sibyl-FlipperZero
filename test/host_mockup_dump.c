/*
 * Emits the exact output of the real engine for a handful of scenarios, in a
 * flat text format the mockup renderer reads.
 *
 * The point is that the screenshots in the README cannot drift away from what
 * the app actually does. Nobody types a confidence figure into the mockup
 * script; it comes from sib_classify, over a pulse train built by the same
 * generator the tests use, so if the scoring changes the pictures change with
 * it or the build fails trying.
 *
 *   ./host_mockup_dump > mockup_data.txt
 */
#include "sib_gen.h"

#include "../helpers/sib_classify.h"
#include "../helpers/sib_features.h"
#include "../helpers/sib_library.h"

#include <stdio.h>
#include <string.h>

static void emit(
    const char* tag,
    SibPulseTrain* t,
    uint8_t repeats,
    uint32_t gap_us,
    uint32_t hz,
    SibMod mod,
    int8_t rssi,
    const char* protocol,
    bool dynamic,
    bool is_static,
    uint16_t bits) {
    SibBurstSummary s[SIB_MAX_BURSTS];
    if(repeats > SIB_MAX_BURSTS) repeats = SIB_MAX_BURSTS;
    sib_repeat_summaries(t, repeats, gap_us, s);

    SibFeatures f;
    sib_features_extract(t, s, repeats, &f);

    SibContext c;
    memset(&c, 0, sizeof(c));
    c.frequency_hz = hz;
    c.mod = mod;
    c.rssi_dbm = rssi;
    if(protocol && *protocol) {
        c.decoded = true;
        strncpy(c.protocol, protocol, sizeof(c.protocol) - 1);
        c.proto_dynamic = dynamic;
        c.proto_static = is_static;
        c.proto_bits = bits;
    }

    SibResult r;
    sib_classify(&f, &c, &r);

    printf("SCENARIO %s\n", tag);
    printf("freq %lu\n", (unsigned long)hz);
    printf("mod %s\n", mod == SibModFsk ? "FM" : "AM");
    printf("rssi %d\n", (int)rssi);
    printf("verdict %s\n", sib_verdict_label(r.verdict));
    printf("confidence %u\n", r.confidence);
    printf("generic %d\n", r.generic_encoder ? 1 : 0);
    printf("note %s\n", r.proto_note);
    for(uint8_t i = 0; i < r.n_cand; i++) {
        printf("cand %u %s\n", r.cand[i].score, sib_class_name(r.cand[i].cls));
    }
    for(uint8_t i = 0; i < r.n_reason; i++) {
        printf("reason %s\n", r.reason[i]);
    }
    printf("class %s\n", sib_class_name(r.n_cand ? r.cand[0].cls : SibClassUnknown));
    printf("tagline %s\n", sib_class_tagline(r.n_cand ? r.cand[0].cls : SibClassUnknown));
    printf("te %u\n", f.te_us);
    printf("fit %u\n", f.fit_permille);
    printf("bits %u\n", f.est_bits);
    printf("repeats %u\n", f.repeats);
    printf("burst_us %lu\n", (unsigned long)f.burst_us);

    printf("first_level %d\n", t->first_level ? 1 : 0);
    printf("pulses");
    for(uint16_t i = 0; i < t->n; i++) printf(" %u", t->dur[i]);
    printf("\n");
    printf("END\n");
}

int main(void) {
    SibPulseTrain t;

    /* A fixed-code OOK handset on 433.92: the crowded corner of the band,
     * where the honest answer is a shortlist. */
    sib_gen_pwm(&t, 350, 24, 3, 1, 31, 5, 0x22u);
    emit("encoder", &t, 8, 11000, 433920000u, SibModOok, -54, "Princeton", false, true, 24);

    /* A decode that names one product family: the only kind that confirms. */
    sib_gen_pwm(&t, 640, 56, 2, 1, 0, 6, 0x11u);
    emit("somfy", &t, 4, 30000, 433420000u, SibModOok, -61, "Somfy Telis", true, false, 56);

    /* Nothing decoded - a tyre sensor, identified purely from its shape. */
    sib_gen_manchester(&t, 52, 72, 8, 0x33u);
    emit("tpms", &t, 2, 55000, 433920000u, SibModFsk, -72, "", false, false, 0);

    return 0;
}
