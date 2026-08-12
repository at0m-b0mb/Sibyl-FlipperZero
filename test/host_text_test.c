/*
 * Word wrapping and ellipsis fitting.
 *
 * Text layout is where this app's real bugs turned up - every screen is 128
 * pixels wide with no scrollbar to save it - so the wrapper is pinned down
 * against a known font metric rather than judged by looking at screenshots.
 *
 * The stubbed canvas measures a fixed six pixels per character, so "fits in
 * 60 px" means exactly "ten characters" and every expectation below is
 * arithmetic instead of opinion.
 */
#include "sib_test.h"

#include "../helpers/sib_text.h"

#include <string.h>

#define PX_PER_CHAR 6

uint16_t canvas_string_width(Canvas* canvas, const char* str) {
    (void)canvas;
    return (uint16_t)(strlen(str) * PX_PER_CHAR);
}

/* The stub never dereferences it; it just has to be non-NULL. */
static Canvas* const CV = (Canvas*)0x1;

static void test_basic_wrap(void) {
    printf("Basic wrapping\n");

    char lines[4][SIB_LINE_MAX];
    /* 60 px = 10 characters per line. */
    uint8_t n = sib_wrap(CV, "aaa bbb ccc ddd", 60, lines, 4);

    CHECK(n == 2, "n = %u, want 2", n);
    CHECK(strcmp(lines[0], "aaa bbb") == 0, "line 0 = \"%s\"", lines[0]);
    CHECK(strcmp(lines[1], "ccc ddd") == 0, "line 1 = \"%s\"", lines[1]);

    for(uint8_t i = 0; i < n; i++) {
        CHECK(canvas_string_width(CV, lines[i]) <= 60, "line %u overran", i);
    }
}

/* ---- the bug this test exists for -------------------------------------
 * A first word wider than the whole line can never fit, so a wrapper that
 * only ever responds to "does not fit" by starting a new line pushes it to
 * line 1 and leaves line 0 empty. On screen that is a blank first row with
 * the text mysteriously starting underneath it.
 */
static void test_overlong_first_word(void) {
    printf("Word wider than the line\n");

    char lines[4][SIB_LINE_MAX];
    uint8_t n = sib_wrap(CV, "supercalifragilistic tail", 60, lines, 4);

    CHECK(n >= 1, "n = %u, want at least 1", n);
    CHECK(lines[0][0] != '\0', "first line came out empty");
    CHECK(strncmp(lines[0], "supercal", 8) == 0, "line 0 = \"%s\"", lines[0]);

    /* And the same word in the middle of a paragraph must not blank a line. */
    n = sib_wrap(CV, "ok supercalifragilistic ok", 60, lines, 4);
    for(uint8_t i = 0; i < n; i++) {
        CHECK(lines[i][0] != '\0', "line %u came out empty", i);
    }
}

static void test_line_budget(void) {
    printf("Line budget is respected\n");

    char lines[2][SIB_LINE_MAX];
    uint8_t n = sib_wrap(CV, "aaa bbb ccc ddd eee fff ggg hhh", 60, lines, 2);
    CHECK(n <= 2, "n = %u, want at most 2", n);

    /* One line only: the caller gets the start of the text, not nothing. */
    char one[1][SIB_LINE_MAX];
    n = sib_wrap(CV, "aaa bbb ccc ddd", 60, one, 1);
    CHECK(n == 1, "n = %u, want 1", n);
    CHECK(strcmp(one[0], "aaa bbb") == 0, "line 0 = \"%s\"", one[0]);
}

static void test_wrap_degenerate(void) {
    printf("Degenerate wrapping\n");

    char lines[4][SIB_LINE_MAX];
    CHECK(sib_wrap(CV, "", 60, lines, 4) == 0, "empty string");
    CHECK(sib_wrap(CV, "   ", 60, lines, 4) == 0, "only spaces");
    CHECK(sib_wrap(NULL, "text", 60, lines, 4) == 0, "NULL canvas");
    CHECK(sib_wrap(CV, NULL, 60, lines, 4) == 0, "NULL source");
    CHECK(sib_wrap(CV, "text", 60, NULL, 4) == 0, "NULL output");
    CHECK(sib_wrap(CV, "text", 60, lines, 0) == 0, "zero lines");
    /* A zero-width line must terminate rather than spin. */
    sib_wrap(CV, "aaa bbb", 0, lines, 4);
}

static void test_fit(void) {
    printf("Ellipsis fitting\n");

    char out[SIB_LINE_MAX];

    sib_fit(CV, "short", 60, out, sizeof(out));
    CHECK(strcmp(out, "short") == 0, "text that fits must be untouched: \"%s\"", out);

    sib_fit(CV, "considerably too long for this", 60, out, sizeof(out));
    CHECK(canvas_string_width(CV, out) <= 60, "fitted text is %u px",
          canvas_string_width(CV, out));
    CHECK(strlen(out) >= 3, "fitted text collapsed to \"%s\"", out);
    CHECK(strstr(out, "..") != NULL, "expected an ellipsis in \"%s\"", out);

    /* Degenerate inputs must not write anywhere they should not. */
    sib_fit(CV, NULL, 60, out, sizeof(out));
    CHECK(out[0] == '\0', "NULL source should clear the output");
    sib_fit(CV, "text", 60, NULL, 10);
    sib_fit(CV, "text", 60, out, 0);
}

/* Every string the app draws on one line has to survive its own budget. */
static void test_real_strings(void) {
    printf("Real UI strings fit their budgets\n");

    /* FontSecondary is narrower than the stub's 6 px per character, so these
     * are deliberately pessimistic: a string that passes here has room to
     * spare on the device. */

    /* Result pages draw page dots from x=106, so their footers stop short. */
    static const char* with_dots[] = {
        "OK explain",
        "Chip, not device",
        "Nothing captured",
    };
    for(size_t i = 0; i < sizeof(with_dots) / sizeof(with_dots[0]); i++) {
        CHECK(canvas_string_width(CV, with_dots[i]) <= 104,
              "\"%s\" is %u px, budget 104 (page dots start at 106)", with_dots[i],
              canvas_string_width(CV, with_dots[i]));
    }

    /* Everywhere else the full width is available. */
    static const char* full_width[] = {
        "< > band",
        "OK reset",
        "OK use band",
        "Hold the remote down",
        "while this sweeps.",
        "What it could be",
        "What it can't tell",
    };
    for(size_t i = 0; i < sizeof(full_width) / sizeof(full_width[0]); i++) {
        CHECK(canvas_string_width(CV, full_width[i]) <= 124, "\"%s\" is %u px, budget 124",
              full_width[i], canvas_string_width(CV, full_width[i]));
    }

    /* The listening screen puts two hints on one row, left and right. */
    CHECK(
        canvas_string_width(CV, "< > band") + canvas_string_width(CV, "OK reset") <= 118,
        "the listening footer's two hints collide");
}

int main(void) {
    test_basic_wrap();
    test_overlong_first_word();
    test_line_budget();
    test_wrap_degenerate();
    test_fit();
    test_real_strings();
    return sib_report("text");
}
