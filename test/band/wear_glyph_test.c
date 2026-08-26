/* Host-side renderer for the clock face's wear indicator — see wear_glyph.h.
 *
 * This is a LOOKING tool, not an assertion tool. Whether a 12 px check still
 * reads as a check is a judgement no pixel comparison can make, and the first
 * version of this glyph compiled, linked and drew strokes that simply did not
 * meet at the elbow. So this prints the glyph as ASCII and a human decides.
 *
 * It shares the real stroke geometry (wear_glyph.h) and the real gfx.c, and
 * replays the driver's SPI stream into a fake panel exactly as band_test.c
 * does — the transport stubs below are lifted from it.
 *
 *   make -C test/band run-wear
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include "st7735s.h"
#include "gfx.h"
#include "fonts.h"

#define PW 128
#define PH 160
static uint16_t panel[PH][PW];
static int p_xs, p_xe, p_ys, p_ye, p_x, p_y;
static int in_ram;          /* RAMWR seen, data now streams into panel */
static int dc_data;
static int n_transfers;     /* data transfers, i.e. visible panel-update passes */
static long n_bytes;        /* total bytes on the wire, commands included */

uint8_t backlight_pct = 100;
int SPI_Init_ST7735(void){ return 0; }
void Pin_CS_Low(void){} void Pin_CS_High(void){}
void Pin_RES_Active(void){} void Pin_RES_Inactive(void){}
void Pin_DC_High(void){ dc_data = 1; } void Pin_DC_Low(void){ dc_data = 0; }
void Pin_BLK_Pct(uint8_t p){ backlight_pct = p; }
void ST7735S_hard_reset_pulse(void){}

void SPI_send(uint16_t len, uint8_t *d)
{
    n_bytes += len;
    if (!dc_data) {                 /* command byte */
        in_ram = 0;
        if (len >= 1) {
            if (d[0] == 0x2a) { p_xs = -1; }        /* CASET pending */
            else if (d[0] == 0x2b) { p_ys = -1; }   /* RASET pending */
            else if (d[0] == 0x2c) { in_ram = 1; p_x = p_xs; p_y = p_ys; }
        }
        return;
    }
    if (p_xs == -1 && len == 4) { p_xs = (d[0]<<8)|d[1]; p_xe = (d[2]<<8)|d[3]; return; }
    if (p_ys == -1 && len == 4) { p_ys = (d[0]<<8)|d[1]; p_ye = (d[2]<<8)|d[3]; return; }
    if (!in_ram) return;

    n_transfers++;
    for (uint16_t i = 0; i + 1 < len; i += 2) {
        if (p_y >= 0 && p_y < PH && p_x >= 0 && p_x < PW) {
            panel[p_y][p_x] = (uint16_t)(d[i] | (d[i+1] << 8));
        }
        if (++p_x > p_xe) { p_x = p_xs; p_y++; }   /* panel auto-wrap */
    }
}
void SPI_TransmitCmd(uint16_t l, uint8_t *d){ Pin_DC_Low();  SPI_send(l, d); }
void SPI_TransmitData(uint16_t l, uint8_t *d){ Pin_DC_High(); SPI_send(l, d); }
void SPI_Transmit(uint16_t l, uint8_t *d){ SPI_TransmitCmd(1, d); d++; if (--l) SPI_TransmitData(l, d); }


/* Mirrors display.c. BACK_* is in struct-field order (the whole Darcula block
 * there is); the wear colours are TRUE red/green/blue and get swapped at the
 * setColor() call, because color565_t's fields are effectively BGR — see the
 * long comment in display.c. That swap is the entire reason this harness now
 * decodes and prints the colour the PANEL sees: the cross shipped blue, and
 * ASCII art cannot show that. */
#define BACK_R 5
#define BACK_G 10
#define BACK_B 5
#define GOOD_RED 11
#define GOOD_GRN 47
#define GOOD_BLU 11
#define BAD_RED  28
#define BAD_GRN  21
#define BAD_BLU  10

#include "wear_glyph.h"

/* panel[] holds each word as it went out on the wire, which is byte-swapped
 * relative to a locally built color565_t. band_test.c's own raw() helper takes
 * 8-bit components and shifts them; the palette above is already in 5/6/5
 * fields, so it needs this variant instead. */
static uint16_t raw565(uint8_t r, uint8_t g, uint8_t b)
{
    color565_t c; c.r = r; c.g = g; c.b = b;
    uint16_t w = (uint16_t)(c.u[0] | (c.u[1] << 8));
    return (uint16_t)((w >> 8) | (w << 8));
}

/* Mirrors printWearField()'s box geometry (display.c). */
static void wear_field(uint32_t posY, uint32_t fieldRight,
                       uint32_t fieldWidth, uint32_t fontHeight, bool worn)
{
    uint32_t fieldLeft = fieldRight - fieldWidth;

    setColor(BACK_R, BACK_G, BACK_B);
    filledRect(fieldLeft - 2, posY - 2, fieldRight + 2, posY + fontHeight + 2);

    uint32_t side = fontHeight;
    uint32_t x1   = fieldRight - 1;
    uint32_t x0   = (x1 > fieldLeft + side) ? (x1 - side) : fieldLeft;

    setColor(worn ? GOOD_BLU : BAD_BLU,
             worn ? GOOD_GRN : BAD_GRN,
             worn ? GOOD_RED : BAD_RED);
    wear_glyph_strokes(x0, posY, side, worn, drawLine);
    flushBuffer();
}

/* Decode a panel word back to what the ST7735S actually displays. panel[] holds
 * the two wire bytes as byte0 | byte1<<8, and RGB565 on the wire is
 * RRRRRGGG GGGBBBBB — so this is the panel's view, not the caller's struct. */
static void report_colour(const char *what, uint16_t w)
{
    uint8_t b0 = (uint8_t)(w & 0xFF), b1 = (uint8_t)(w >> 8);
    int r5 = b0 >> 3;
    int g6 = ((b0 & 0x07) << 3) | (b1 >> 5);
    int b5 = b1 & 0x1F;
    printf("  panel sees %s: R=%2d G=%2d B=%2d  ->  #%02X%02X%02X  (%s)\n",
           what, r5, g6, b5, r5 << 3, g6 << 2, b5 << 3,
           (r5 > b5 + 6 && r5 > g6) ? "RED"
         : (g6 / 2 > r5 && g6 / 2 > b5) ? "GREEN"
         : (b5 > r5 + 6) ? "BLUE" : "other");
}

static void show(const char *what, int posY, int fieldRight, int fieldWidth, int h)
{
    printf("\n%s   (cleared box x %d..%d, y %d..%d)\n", what,
           fieldRight - fieldWidth - 2, fieldRight + 2, posY - 2, posY + h + 2);
    for (int y = posY - 3; y <= posY + h + 3; y++) {
        printf("  %3d |", y);
        for (int x = fieldRight - fieldWidth - 3; x <= fieldRight + 3; x++) {
            uint16_t p = panel[y][x];
            putchar(p == 0xFFFF ? '.' : (p == raw565(BACK_R, BACK_G, BACK_B) ? ' ' : '#'));
        }
        printf("|\n");
    }
    /* Find a glyph pixel and say what colour the panel will show it as. */
    for (int y = posY; y <= posY + h; y++)
        for (int x = fieldRight - fieldWidth; x <= fieldRight; x++) {
            uint16_t p = panel[y][x];
            if (p != 0xFFFF && p != raw565(BACK_R, BACK_G, BACK_B)) {
                report_colour(what, p);
                return;
            }
        }
    printf("  FAIL: no glyph pixels found\n");
}

int main(void)
{
    /* The real clock-face numbers: CLOCK_WEAR_Y_MARGIN 50, CLOCK_WEAR_RIGHT
     * WIDTH-2, CLOCK_WEAR_WIDTH 16, CLOCK_STATUS_FONT FONT_SMALL (12). */
    const int Y = 160 - 50, RIGHT = 128 - 2, W = 16, H = 12;

    ST7735S_Init();

    memset(panel, 0xFF, sizeof panel);
    wear_field(Y, RIGHT, W, H, true);
    show("WORN -> green check", Y, RIGHT, W, H);

    memset(panel, 0xFF, sizeof panel);
    wear_field(Y, RIGHT, W, H, false);
    show("NOT WORN -> red cross", Y, RIGHT, W, H);

    /* The BT badge sits one row below at HEIGHT - CLOCK_SOC_TEMP_Y_MARGIN.
     * Both boxes clear before they draw, so an overlap would make the result
     * depend on which dirty flag was serviced last. Worth failing loudly. */
    int wear_bottom = Y + H + 2;
    int bt_top      = 160 - 34 - 2;
    printf("\nwear box rows %d..%d ; BT box rows %d..%d -> %s\n",
           Y - 2, wear_bottom, bt_top, 160 - 34 + H + 2,
           wear_bottom <= bt_top ? "no overlap" : "*** OVERLAP ***");
    if (wear_bottom > bt_top) {
        printf("FAIL: wear and BT badge boxes overlap\n");
        return 1;
    }
    return 0;
}
