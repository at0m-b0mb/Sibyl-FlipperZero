/**
 * Sibyl - text layout for a 128 px line.
 *
 * The library's explainers are prose, and prose has to be wrapped somewhere.
 * Doing it here, measured against the real canvas font rather than guessed
 * from character counts, means the copy can be edited without anyone having
 * to re-count columns.
 */
#pragma once

#include <gui/canvas.h>

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SIB_LINE_MAX 44

/**
 * Greedy word-wrap `src` into at most `max_lines` lines of at most `max_w`
 * pixels each, in whatever font the canvas currently has. Returns the number
 * of lines written. Text that will not fit is dropped, so callers that must
 * not lose any should give a generous `max_lines` and scroll.
 */
uint8_t sib_wrap(
    Canvas* canvas,
    const char* src,
    int max_w,
    char lines[][SIB_LINE_MAX],
    uint8_t max_lines);

/** Copy `src` into `out`, truncating with an ellipsis to fit `max_w` pixels. */
void sib_fit(Canvas* canvas, const char* src, int max_w, char* out, size_t out_sz);

#ifdef __cplusplus
}
#endif
