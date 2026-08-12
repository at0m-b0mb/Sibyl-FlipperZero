/* Host stub. Just enough of gui/canvas.h to compile the text layout helper
 * off the Flipper; the test supplies canvas_string_width itself so the metric
 * is known exactly and the wrapping can be asserted rather than eyeballed. */
#pragma once

#include <stddef.h>
#include <stdint.h>

typedef struct Canvas Canvas;

uint16_t canvas_string_width(Canvas* canvas, const char* str);
