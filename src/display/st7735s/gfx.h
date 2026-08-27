#ifndef __GFX_H__
#define __GFX_H__

#include <inttypes.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void setPixel(uint16_t, uint16_t);
/* background pixel used for font draw */
void setbgPixel(uint16_t, uint16_t);
/* Fills the whole panel with the FOREGROUND colour (setColor/setColorRaw), not
 * the background one — it is filledRect(0,0,W,H), and every fill primitive here
 * paints with `color`. `bg_color` is read only by the font renderer's opaque
 * mode via setbgPixel(). Set the foreground to whatever you want the screen to
 * become before calling this; see clear_display() in display.c, which got this
 * wrong for a long time and painted the panel black on the day the menu ink
 * became black. */
void fillScreen(void);
/* needs to be the last action when using BUFFER or HVBUFFER */
void flushBuffer(void);
/* Off-screen composite band: everything drawn between beginBand() and
 * endBand() that lands inside the rect goes out as one transfer instead of
 * appearing progressively. See ST7735S_bandBegin() in st7735s.h for the rules
 * — in particular, call endBand() if and only if beginBand() returned true. */
bool beginBand(uint16_t x, uint16_t y, uint16_t w, uint16_t h);
void endBand(void);
void setFont(const uint8_t *);
void drawText(uint16_t, uint16_t, const char *);
void drawCircle(uint16_t, uint16_t, uint16_t);
void filledCircle(uint16_t, uint16_t, uint16_t);
void drawArc(uint16_t, uint16_t, uint16_t, float, float);
void drawPie(uint16_t, uint16_t, uint16_t, float, float);
void drawLine(uint16_t,uint16_t, uint16_t, uint16_t);
void setColorRaw(color565_t);
void setbgColorRaw(color565_t);
void setColor(uint8_t, uint8_t, uint8_t);
void setTransparent(bool);
/* background color for fonts if !transparent */
void setbgColor(uint8_t, uint8_t, uint8_t);
void drawRect(uint16_t, uint16_t, uint16_t, uint16_t);
void filledRect(uint16_t, uint16_t, uint16_t, uint16_t);
void setColor24(uint32_t);
void setbgColor24(uint32_t);

#ifdef __cplusplus
} // extern "C"
#endif

#endif

