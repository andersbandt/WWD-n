#ifndef __ST7735S_h__
#define __ST7735S_h__

#ifdef __cplusplus
extern "C" {
#endif

#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>

#include "st7735s_compat.h"

/* undef if low on mem */
#if !defined (BUFFER) && !defined (BUFFER1) && !defined (HVBUFFER)
  #warning no buffer defined, defining BUFFER1
  #define BUFFER1
#endif

typedef enum { R0, R90, R180, R270 } rotation_t;
typedef enum { ON, OFF } idlemode_t;

typedef struct  {
    union {
        struct {
            uint16_t r:5;
            uint16_t g:6;
            uint16_t b:5;
        } __attribute__((packed));
        uint8_t u[2];
    };
} __attribute__((packed)) color565_t;

extern uint16_t WIDTH, HEIGHT;
extern uint16_t XSTART, YSTART;  /* was "XSTART, XSTART" - YSTART had no
                                  * declaration here at all and only linked
                                  * because st7735s.c defines both at file
                                  * scope. Both are 0 on this panel, so the
                                  * typo never produced a visible symptom. */

extern color565_t color;
extern color565_t bg_color;

void Delay(uint32_t);
void Backlight_Pct(uint8_t p);

/* ---- transient backlight (fades) -----------------------------------------
 *
 * Backlight_Pct()/Pin_BLK_Pct() both *remember* the level they set, and
 * ST7735S_sleepOut() restores that remembered level on wake. So a fade driven
 * through them would walk the remembered level down to 0 along with the actual
 * PWM, and the panel would come back from sleep black — the user's brightness
 * silently destroyed by the animation.
 *
 * These two drive the PWM without touching what is remembered:
 *   Transient() sets the duty for right now.
 *   Restore()   puts the PWM back to the remembered level.
 *
 * ST7735S_sleepIn() already does exactly this save/restore dance internally
 * for the same reason; these just expose it so an animation can step the level
 * repeatedly. Neither touches SPI — the backlight is its own PWM peripheral —
 * so neither needs the display draw lock. That is what lets a fade run without
 * blocking the drawing threads.
 */
void ST7735S_backlightTransient(uint8_t pct);
void ST7735S_backlightRestore(void);
int ST7735S_Init(void);
void ST7735S_flush(void);

/* ---- off-screen composite band -------------------------------------------
 *
 * Between bandBegin() and bandEnd(), every pixel landing inside the requested
 * rect is written to RAM instead of straight out over SPI; bandEnd() sends the
 * whole rect as one contiguous RAMWR. The point is the clear-then-draw pattern
 * used throughout display.c (filledRect() in the background colour, then
 * drawText() over it): unbuffered, the panel visibly shows the cleared box
 * before the glyphs arrive, which is the flicker. Composited, the field
 * changes in a single transfer.
 *
 * The band is seeded with bg_color at begin, because the panel cannot be read
 * back on this board — anything inside the rect that the caller does not draw
 * comes out as background, not as whatever was there before. Use it for rects
 * the caller fully repaints; that is what every display.c field helper does.
 *
 * bandBegin() takes hvbuffer_mutex and bandEnd() releases it, so the whole
 * composite is atomic against the other drawing threads (ui_refresh_thread and
 * button_handler_thread both draw — see the note above hvbuffer_mutex in
 * st7735s.c). Every successful bandBegin() MUST be paired with a bandEnd() on
 * the same thread or the display locks up; keep the pair inside one function.
 * Nesting is refused rather than silently mis-composited.
 *
 * Returns false — and takes no lock, so do NOT call bandEnd() — if the band is
 * compiled out (ST7735S_BAND_ROWS 0), the rect is taller than the band, the
 * rect is empty, or a band is already active. On false the caller should just
 * draw as it always did; the result is correct either way, only flickerier.
 *
 * Pixels drawn outside the rect while a band is active still go out over the
 * ordinary run-length path, so nothing is silently dropped.
 */
bool ST7735S_bandBegin(uint16_t x, uint16_t y, uint16_t w, uint16_t h);
void ST7735S_bandEnd(void);
bool ST7735S_bandActive(void);
void ST7735S_Pixel(uint16_t x, uint16_t y);
void ST7735S_bgPixel(uint16_t x, uint16_t y);
void setOrientation(rotation_t r);
void ST7735S_sleepIn(void);
void ST7735S_sleepOut(void);
bool ST7735S_defineScrollArea(uint16_t, uint16_t);
void ST7735S_tearingOn(bool);
void ST7735S_tearingOff(void);
void ST7735S_partialArea(uint16_t, uint16_t);
void ST7735S_normalMode(void);
void ST7735S_scroll(uint8_t);
#ifdef __cplusplus
} // extern "C"
#endif

#endif

