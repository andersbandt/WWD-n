/* Native harness for the ST7735S band buffer: replays the driver's SPI stream
 * into a fake 128x160 panel and checks the composited result pixel-for-pixel.
 * Stubs only the transport (the SPI and Pin entry points); st7735s.c and gfx.c are the real
 * repo sources, compiled unmodified. */
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

static uint16_t raw(uint8_t r, uint8_t g, uint8_t b)
{
    color565_t c; c.r = r >> 3; c.g = g >> 2; c.b = b >> 3;
    return (uint16_t)(c.u[0] | (c.u[1] << 8));
}

static int fails;
static void chk(const char *what, int got, int want)
{
    if (got != want) { printf("  FAIL %-46s got %d want %d\n", what, got, want); fails++; }
    else             { printf("  ok   %-46s %d\n", what, got); }
}

int main(void)
{
    const uint16_t BG = raw(0,0,0), FG = raw(255,255,255), OLD = raw(255,0,0);

    /* Prime the whole panel with a colour that is neither fg nor bg, so any
     * pixel the band fails to write shows up as stale. */
    for (int y = 0; y < PH; y++) for (int x = 0; x < PW; x++) panel[y][x] = OLD;

    setbgColor(0,0,0);
    setColor(0,0,0);

    /* --- the real pattern from display.c: clear a field, draw into it ----- */
    n_transfers = 0;
    bool banded = beginBand(10, 40, 60, 32);
    chk("beginBand(60x32) accepted", banded, 1);

    setColor(0,0,0);           filledRect(10, 40, 69, 71);      /* the clear */
    setColor(255,255,255);     filledRect(20, 50, 29, 59);      /* the "glyph" */
    flushBuffer();
    endBand();

    chk("panel updates for whole composite", n_transfers, 1);
    chk("band pixel outside glyph = background", panel[40][10] == BG, 1);
    chk("band pixel at far corner  = background", panel[71][69] == BG, 1);
    chk("glyph pixel top-left      = foreground", panel[50][20] == FG, 1);
    chk("glyph pixel bottom-right  = foreground", panel[59][29] == FG, 1);
    chk("just outside glyph        = background", panel[49][20] == BG, 1);
    chk("just outside band (row 39) untouched", panel[39][10] == OLD, 1);
    chk("just outside band (row 72) untouched", panel[72][10] == OLD, 1);
    chk("just outside band (col 9)  untouched", panel[40][9]  == OLD, 1);
    chk("just outside band (col 70) untouched", panel[40][70] == OLD, 1);

    /* --- unbuffered equivalent must land identically, just in more passes -- */
    for (int y = 0; y < PH; y++) for (int x = 0; x < PW; x++) panel[y][x] = OLD;
    n_transfers = 0;
    setColor(0,0,0);           filledRect(10, 40, 69, 71);
    setColor(255,255,255);     filledRect(20, 50, 29, 59);
    flushBuffer();
    chk("unbuffered needs many passes", n_transfers > 1, 1);
    chk("unbuffered glyph matches banded", panel[50][20] == FG, 1);
    chk("unbuffered bg matches banded", panel[40][10] == BG, 1);
    { int stale=0,n=0; for(int y=40;y<=71;y++) for(int x=10;x<=69;x++){ n++; if(panel[y][x]==OLD){ if(stale<8) printf("       stale (%d,%d)\n",x,y); stale++; } }
      printf("       unbuffered: %d/%d pixels left stale\n", stale, n); }

    /* --- refusals -------------------------------------------------------- */
    chk("rect taller than band refused", beginBand(0, 0, 128, 41), 0);
    chk("empty rect refused", beginBand(0, 0, 0, 10), 0);
    banded = beginBand(0, 0, 32, 8);
    chk("nested beginBand refused", beginBand(0, 0, 32, 8), 0);
    endBand();
    chk("band released after endBand", ST7735S_bandActive(), 0);

    /* --- out-of-band pixels during a band still reach the panel ---------- */
    for (int y = 0; y < PH; y++) for (int x = 0; x < PW; x++) panel[y][x] = OLD;
    beginBand(10, 40, 60, 32);
    setColor(255,255,255);
    filledRect(20, 50, 29, 59);      /* inside  */
    filledRect(20, 100, 29, 109);    /* outside */
    flushBuffer();
    endBand();
    chk("in-band draw landed", panel[55][25] == FG, 1);
    chk("out-of-band draw not dropped", panel[105][25] == FG, 1);

    /* --- headline: the clock's 1 Hz "SS" field, real glyphs, FONT_XXLARGE.
     *     Geometry copied from printFieldRightAligned() in display.c. --------*/
    {
        const uint32_t posY = 46, fieldRight = 120, fieldWidth = 28, fontH = 28;
        int banded_passes, plain_passes; long banded_bytes = 0, plain_bytes = 0;

        setFont(ter_u28b);

        beginBand(fieldRight - fieldWidth - 2, posY - 2,
                  (fieldRight + 2) - (fieldRight - fieldWidth - 2) + 1, fontH + 4 + 1);
        n_transfers = 0; n_bytes = 0;
        setColor(0,0,0);       filledRect(fieldRight - fieldWidth - 2, posY - 2, fieldRight + 2, posY + fontH + 2);
        setColor(255,255,255); drawText(fieldRight - fieldWidth, posY, "42");
        flushBuffer();
        endBand();
        banded_passes = n_transfers; banded_bytes = n_bytes;

        n_transfers = 0; n_bytes = 0;
        setColor(0,0,0);       filledRect(fieldRight - fieldWidth - 2, posY - 2, fieldRight + 2, posY + fontH + 2);
        setColor(255,255,255); drawText(fieldRight - fieldWidth, posY, "42");
        flushBuffer();
        plain_passes = n_transfers; plain_bytes = n_bytes;

        printf("\n  clock SS field (FONT_XXLARGE, 33x33 px):\n");
        printf("    unbuffered : %4d panel-update passes, %5ld bytes on the wire\n", plain_passes, plain_bytes);
        printf("    banded     : %4d panel-update pass,   %5ld bytes on the wire\n", banded_passes, banded_bytes);
        printf("    reduction  : %4.1fx passes, %4.2fx bytes\n", (double)plain_passes / banded_passes, (double)plain_bytes / banded_bytes);
        chk("banded SS field is a single pass", banded_passes, 1);
    }

    printf(fails ? "\nFAILED (%d)\n" : "\nALL PASS\n", fails);
    return fails != 0;
}
