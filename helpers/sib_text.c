#include "sib_text.h"

#include <stdio.h>
#include <string.h>

uint8_t sib_wrap(
    Canvas* canvas,
    const char* src,
    int max_w,
    char lines[][SIB_LINE_MAX],
    uint8_t max_lines) {
    if(!canvas || !src || !lines || max_lines == 0) return 0;

    uint8_t n = 0;
    lines[0][0] = '\0';

    const char* p = src;
    char word[SIB_LINE_MAX];
    char cand[SIB_LINE_MAX * 2];

    while(*p && n < max_lines) {
        while(*p == ' ') p++;
        if(!*p) break;

        size_t wl = 0;
        while(*p && *p != ' ' && wl < sizeof(word) - 1) word[wl++] = *p++;
        word[wl] = '\0';
        /* A single word longer than the buffer would otherwise loop forever. */
        while(*p && *p != ' ') p++;

        if(lines[n][0] == '\0') {
            snprintf(cand, sizeof(cand), "%s", word);
        } else {
            snprintf(cand, sizeof(cand), "%s %s", lines[n], word);
        }

        /* The empty-line test is not redundant: a word wider than the whole
         * line never fits, and without it the wrapper would push it onto a
         * fresh line for ever, leaving the current one blank. Clipped is
         * better than dropped, and far better than an empty first row. */
        if(canvas_string_width(canvas, cand) <= max_w || lines[n][0] == '\0') {
            strncpy(lines[n], cand, SIB_LINE_MAX - 1);
            lines[n][SIB_LINE_MAX - 1] = '\0';
        } else {
            n++;
            if(n >= max_lines) break;
            strncpy(lines[n], word, SIB_LINE_MAX - 1);
            lines[n][SIB_LINE_MAX - 1] = '\0';
        }
    }

    if(n < max_lines && lines[n][0] != '\0') n++;
    return n;
}

void sib_fit(Canvas* canvas, const char* src, int max_w, char* out, size_t out_sz) {
    if(!out || out_sz == 0) return;
    out[0] = '\0';
    if(!src) return;

    strncpy(out, src, out_sz - 1);
    out[out_sz - 1] = '\0';
    if(!canvas || canvas_string_width(canvas, out) <= max_w) return;

    size_t len = strlen(out);
    while(len > 1) {
        len--;
        char probe[SIB_LINE_MAX * 2];
        size_t n = len;
        if(n > sizeof(probe) - 3) n = sizeof(probe) - 3;
        memcpy(probe, src, n);
        probe[n] = '.';
        probe[n + 1] = '.';
        probe[n + 2] = '\0';
        if(canvas_string_width(canvas, probe) <= max_w) {
            strncpy(out, probe, out_sz - 1);
            out[out_sz - 1] = '\0';
            return;
        }
    }
}
