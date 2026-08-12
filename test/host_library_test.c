/*
 * The reference library: the words the app puts on screen and the protocol
 * table behind them.
 *
 * Content tests look unglamorous next to signal processing, but every string
 * in here is drawn onto a 128 x 64 display with no scrollbar to save it, and
 * a class with an empty explainer is a blank screen in the user's hand. So
 * the invariants are checked over the whole table, not over the two rows
 * someone thought to spot-check.
 */
#include "sib_test.h"

#include "../helpers/sib_classify.h"
#include "../helpers/sib_library.h"

#include <string.h>

/* Budgets, in characters, for the places these strings are drawn. The result
 * screen sets the class name in FontPrimary across the full width; taglines
 * and explainers are wrapped by the view at 21 columns of FontSecondary. */
#define SIB_NAME_BUDGET 18
#define SIB_TAGLINE_BUDGET 52

static void test_every_class_is_complete(void) {
    printf("Class table completeness\n");

    for(uint8_t c = 1; c < SibClassCount; c++) {
        SibClass cls = (SibClass)c;
        const char* name = sib_class_name(cls);
        const char* tag = sib_class_tagline(cls);
        const SibClassEntry* e = sib_class_entry(cls);

        CHECK(name && *name, "class %u has no name", c);
        CHECK(tag && *tag, "class %u (%s) has no tagline", c, name);
        CHECK(e != NULL, "class %u (%s) has no entry", c, name);

        CHECK(strlen(name) <= SIB_NAME_BUDGET, "name \"%s\" is %zu chars, budget %d",
              name, strlen(name), SIB_NAME_BUDGET);
        CHECK(strlen(tag) <= SIB_TAGLINE_BUDGET, "tagline for %s is %zu chars", name,
              strlen(tag));

        CHECK(e->what && strlen(e->what) > 40, "%s: 'what' is too thin", name);
        CHECK(e->how && strlen(e->how) > 40, "%s: 'how' is too thin", name);
        CHECK(e->security && strlen(e->security) > 40, "%s: 'security' is too thin",
              name);

        /* The limits section is the one that keeps this honest, so it is not
         * allowed to be an afterthought. */
        CHECK(e->limits && strlen(e->limits) > 40, "%s: 'limits' is too thin", name);
    }

    /* Out-of-range must degrade to the Unknown row, not walk off the table. */
    CHECK(sib_class_name(SibClassCount) != NULL, "out-of-range name");
    CHECK(sib_class_tagline((SibClass)200) != NULL, "out-of-range tagline");
    CHECK(sib_class_entry((SibClass)200) != NULL, "out-of-range entry");
}

static void test_protocol_table_invariants(void) {
    printf("Protocol table invariants\n");

    uint16_t n = sib_protocol_count();
    CHECK(n > 20, "protocol table is only %u rows", n);

    for(uint16_t i = 0; i < n; i++) {
        const char* kw = NULL;
        SibProtoInfo info;
        CHECK(sib_protocol_at(i, &kw, &info), "row %u unreadable", i);
        CHECK(kw && *kw, "row %u has no keyword", i);
        CHECK(info.note && *info.note, "row %u (%s) has no note", i, kw ? kw : "?");
        CHECK(strlen(info.note) < SIB_NOTE_LEN, "row %s note is %zu chars, buffer %d",
              kw, strlen(info.note), SIB_NOTE_LEN);
        CHECK(info.cls < SibClassCount, "row %s has class %u", kw, info.cls);

        /* Keywords are matched case-insensitively against the decoder's name,
         * so a keyword with an upper-case letter in it can never match. */
        for(const char* p = kw; *p; p++) {
            CHECK(!(*p >= 'A' && *p <= 'Z'), "keyword \"%s\" must be lower case", kw);
        }

        /* The central distinction of the whole app: a row either names a
         * product family (and must say which class) or names an encoder chip
         * (and must not pretend to). */
        if(info.device_specific) {
            CHECK(info.cls != SibClassUnknown, "%s is device-specific but has no class",
                  kw);
        } else {
            CHECK(info.cls == SibClassUnknown,
                  "%s is an encoder chip but claims class %s", kw,
                  sib_class_name(info.cls));
            CHECK(info.bias_mask != 0, "%s is a chip with no plausible classes", kw);
        }

        /* A device-specific row should list its own class as plausible. */
        if(info.device_specific) {
            CHECK((info.bias_mask & SIB_BIT(info.cls)) != 0,
                  "%s does not list its own class in bias_mask", kw);
        }
    }

    CHECK(!sib_protocol_at(n, NULL, NULL), "index past the end must fail");
    CHECK(!sib_protocol_at(60000, NULL, NULL), "far out of range must fail");
}

/* Earlier rows win, so a general keyword must never shadow a specific one. */
static void test_keyword_ordering(void) {
    printf("Keyword shadowing\n");

    uint16_t n = sib_protocol_count();
    for(uint16_t i = 0; i < n; i++) {
        const char* a = NULL;
        sib_protocol_at(i, &a, NULL);
        for(uint16_t j = i + 1; j < n; j++) {
            const char* b = NULL;
            sib_protocol_at(j, &b, NULL);
            /* If an earlier keyword is contained in a later one, the later row
             * is unreachable: any name matching it matches the earlier row
             * first. */
            CHECK(strstr(b, a) == NULL, "row %u \"%s\" is shadowed by row %u \"%s\"", j,
                  b, i, a);
        }
    }
}

static void test_lookup(void) {
    printf("Protocol lookup\n");

    struct {
        const char* decoded;
        SibClass want;
        bool specific;
    } cases[] = {
        {"Somfy Telis", SibClassBlinds, true},
        {"Somfy Keytis", SibClassBlinds, true},
        {"Security+ 2.0", SibClassGateRemote, true},
        {"Security+ 1.0", SibClassGateRemote, true},
        {"Hormann HSM", SibClassGateRemote, true},
        {"Nice FLO", SibClassGateRemote, true},
        {"Nice FloR-S", SibClassGateRemote, true},
        {"CAME", SibClassGateRemote, true},
        {"CAME Atomo", SibClassGateRemote, true},
        {"FAAC SLH", SibClassGateRemote, true},
        {"Star Line", SibClassCarFob, true},
        {"Scher-Khan", SibClassCarFob, true},
        {"Magellan", SibClassSensor, true},
        {"Dooya", SibClassBlinds, true},
        {"Alutech AT-4N", SibClassGateRemote, true},
        {"Intertechno_V3", SibClassSocket, true},
        /* the chips */
        {"Princeton", SibClassUnknown, false},
        {"KeeLoq", SibClassUnknown, false},
        {"Holtek_HT12X", SibClassUnknown, false},
        {"SMC5326", SibClassUnknown, false},
    };

    for(size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        SibProtoInfo info;
        bool found = sib_protocol_lookup(cases[i].decoded, &info);
        CHECK(found, "\"%s\" not found in the library", cases[i].decoded);
        if(!found) continue;
        CHECK(info.cls == cases[i].want, "\"%s\" -> %s, want %s", cases[i].decoded,
              sib_class_name(info.cls), sib_class_name(cases[i].want));
        CHECK(info.device_specific == cases[i].specific, "\"%s\" specific = %d",
              cases[i].decoded, info.device_specific);
    }

    /* Matching is case-insensitive because the firmware's spelling drifts. */
    SibProtoInfo a, b;
    CHECK(sib_protocol_lookup("SOMFY TELIS", &a), "upper case should match");
    CHECK(sib_protocol_lookup("somfy telis", &b), "lower case should match");
    CHECK(a.cls == b.cls && a.cls == SibClassBlinds, "case changed the answer");

    /* Unknown and degenerate input. */
    SibProtoInfo z;
    CHECK(!sib_protocol_lookup("Zaphod Beeblebrox", &z), "unknown must not match");
    CHECK(z.cls == SibClassUnknown, "failed lookup must zero its output");
    CHECK(!sib_protocol_lookup("", &z), "empty must not match");
    CHECK(!sib_protocol_lookup(NULL, &z), "NULL must not match");
    CHECK(sib_protocol_lookup("Princeton", NULL), "a NULL output must still match");
}

static void test_verdict_labels(void) {
    printf("Verdict labels\n");
    CHECK(strcmp(sib_verdict_label(SibVerdictConfirmed), "CONFIRMED") == 0, "confirmed");
    CHECK(strcmp(sib_verdict_label(SibVerdictLikely), "LIKELY") == 0, "likely");
    CHECK(strcmp(sib_verdict_label(SibVerdictPossible), "POSSIBLE") == 0, "possible");
    CHECK(sib_verdict_label(SibVerdictUnknown) != NULL, "unknown");
    CHECK(sib_verdict_label((SibVerdict)99) != NULL, "out of range");
    for(int v = 0; v <= 4; v++) {
        CHECK(strlen(sib_verdict_label((SibVerdict)v)) <= 12, "verdict label too wide");
    }
}

int main(void) {
    test_every_class_is_complete();
    test_protocol_table_invariants();
    test_keyword_ordering();
    test_lookup();
    test_verdict_labels();
    return sib_report("library");
}
