#ifndef WEAR_GLYPH_H
#define WEAR_GLYPH_H

#include <stdbool.h>
#include <stdint.h>

/*
 * Stroke geometry for the clock face's wear indicator — the green check
 * (on-wrist) and the red cross (off-wrist) drawn by printWearField() in
 * display.c.
 *
 * This lives in its own header, parameterised on the line-drawing function,
 * for exactly one reason: `test/band/wear_glyph_test.c` renders these glyphs
 * host-side to ASCII so a human can see whether they still read as a check and
 * a cross. Legibility at 12 px is not something a build or a pixel assertion
 * can judge, and the first cut of this shipped strokes that did not meet at
 * the elbow — it compiled and linked perfectly and looked like a smudge. If
 * the coordinates were duplicated into the harness instead of shared, the
 * harness would keep rendering the old glyph after any tweak here and would be
 * worse than useless. So: one definition, two callers.
 *
 * Nothing here touches the palette, the band, or the panel — the caller sets
 * the color and clears the box. This is only the shape.
 */

/*
 * @param x0    left edge of the (square) glyph box
 * @param y0    top edge
 * @param side  box side length in pixels; sized to the neighbouring font's
 *              cap height so the badge lines up with the text badges
 * @param worn  true for the check, false for the cross
 * @param line  drawLine(x0, y0, x1, y1) — gfx.c's, or the harness's
 */
static inline void wear_glyph_strokes(uint32_t x0, uint32_t y0, uint32_t side,
                                      bool worn,
                                      void (*line)(uint16_t, uint16_t,
                                                   uint16_t, uint16_t))
{
    uint32_t x1 = x0 + side;
    uint32_t y1 = y0 + side - 1;

    /* Thicken a diagonal by drawing it three times: as-is, +1 in x, +1 in y.
     * A single offset only thickens perpendicular to one axis, so doubling
     * both strokes the same way leaves the steeper one looking dotted while
     * the shallow one looks solid — which is precisely what made the first
     * version of the check illegible. Three passes cover any slope, and at
     * six strokes total it is far cheaper than anti-aliasing. */
    #define WG_STROKE(ax, ay, bx, by) do {                                  \
        line((uint16_t)(ax),     (uint16_t)(ay),                            \
             (uint16_t)(bx),     (uint16_t)(by));                           \
        line((uint16_t)(ax) + 1, (uint16_t)(ay),                            \
             (uint16_t)(bx) + 1, (uint16_t)(by));                           \
        line((uint16_t)(ax),     (uint16_t)(ay) + 1,                        \
             (uint16_t)(bx),     (uint16_t)(by) + 1);                       \
    } while (0)

    if (worn) {
        /* Check: a short steep down-stroke into the elbow, then the long
         * shallow up-stroke. The elbow sits 1/3 across and one pixel off the
         * bottom edge — the proportion that still reads as a tick at 12 px
         * rather than as a V. */
        uint32_t ex = x0 + side / 3;
        uint32_t ey = y1 - 1;

        WG_STROKE(x0, y0 + side / 2, ex, ey);
        WG_STROKE(ex, ey, x1 - 1, y0 + 1);
    } else {
        WG_STROKE(x0, y0, x1 - 1, y1 - 1);
        WG_STROKE(x1 - 1, y0, x0, y1 - 1);
    }

    #undef WG_STROKE
}

#endif /* WEAR_GLYPH_H */
