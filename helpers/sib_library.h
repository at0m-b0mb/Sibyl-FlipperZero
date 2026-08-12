/**
 * Sibyl - the reference library.
 *
 * Two tables and the words that go with them:
 *
 *   1. Device classes: display name, one-line tagline, and the four-part
 *      explainer the app shows once it has named something - what the device
 *      is, how its radio link works, what that means for security, and what
 *      Sibyl still cannot tell you about it. The last part is not padding;
 *      it is the difference between a teaching tool and a magic 8-ball.
 *
 *   2. Protocols: the mapping from a decoder name to a device class. Matching
 *      is by case-insensitive substring rather than exact string, because the
 *      firmware renames protocols between releases ("Came Atomo" vs
 *      "CAME Atomo") and a classifier that silently stops recognising a
 *      protocol after an SDK bump is worse than one that never knew it.
 *
 *      Crucially, entries are split into protocols that name a *product*
 *      (Somfy RTS only ever comes out of a blind motor) and protocols that
 *      name an *encoder chip* (a Princeton packet could be a gate, a doorbell
 *      or a mains socket). Only the first kind can confirm anything.
 */
#pragma once

#include "sib_classify.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Short display name, e.g. "Gate remote". Never NULL. */
const char* sib_class_name(SibClass cls);

/** One-line description for under the headline. Never NULL. */
const char* sib_class_tagline(SibClass cls);

/** The four-part explainer. Never NULL for a valid class. */
typedef struct {
    const char* what; /* what the device does                  */
    const char* how; /* how its radio link is built           */
    const char* security; /* what that means for security          */
    const char* limits; /* what Sibyl cannot tell you about it   */
} SibClassEntry;

const SibClassEntry* sib_class_entry(SibClass cls);

/* What we know about a decoded protocol. */
typedef struct {
    SibClass cls; /* SibClassUnknown for encoder chips     */
    bool device_specific; /* true = names a product family         */
    uint16_t bias_mask; /* 1 << SibClass, classes it could be    */
    const char* note; /* one line, shown as evidence           */
} SibProtoInfo;

/**
 * Look a decoder name up. Returns false when the protocol is unknown to the
 * library, in which case `out` is left zeroed and the fingerprint decides
 * on its own.
 */
bool sib_protocol_lookup(const char* protocol, SibProtoInfo* out);

/**
 * Walk the protocol table. Backs the in-app library browser, and lets the
 * tests assert the table's invariants over every row rather than over a
 * handful of examples someone remembered to add.
 *
 * `keyword` receives the matching keyword (a static string, do not free).
 * Either output may be NULL. Returns false once `index` runs past the end.
 */
uint16_t sib_protocol_count(void);
bool sib_protocol_at(uint16_t index, const char** keyword, SibProtoInfo* out);

/** Handy in tests and in the bias tables. */
#define SIB_BIT(cls) ((uint16_t)1u << (cls))

#ifdef __cplusplus
}
#endif
