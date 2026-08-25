#ifndef __st7735s_compat_h__
#define __st7735s_compat_h__

#include <inttypes.h>

/* Physical panel is 128x160, not 180 — anything drawn near row 160-179 with
 * the old value fell off the visible glass entirely (confirmed on hardware:
 * the step-count badge at HEIGHT-20 was invisible because the screen
 * physically ends before that row). */
#define defWIDTH   128
#define defHEIGHT  160
#define defXSTART  0
#define defYSTART  0

/* BUFFER: full frame buffer. Don't think it will fit into nRF52832 RAM*/
// #define BUFFER

/* BUFFER1: slowest, used for limited RAM */
// #define BUFFER1

/*HVBUFFER: takes advantage of writing adjacent same color pixels*/
#define HVBUFFER

/* ST7735S_BAND_ROWS: height, in panel rows, of the off-screen composite band
 * (see ST7735S_bandBegin() in st7735s.h). Costs defWIDTH * ST7735S_BAND_ROWS *
 * 2 bytes of BSS: 40 rows = 10,240 B. 40 is not arbitrary — it is the tallest
 * text field the UI can ask for (FONT_HUGE = 32 px plus the +/-2 px clear
 * margin display.c adds on each side = 36 rows), rounded up. Drop it and the
 * largest fields silently fall back to the unbuffered path rather than
 * breaking; a full frame would be 128*160*2 = 40,960 B and does not fit
 * alongside BLE.
 *
 * Set to 0 to compile the band buffer out entirely and get the old behaviour
 * back — every bandBegin() then reports failure and callers draw direct. */
#ifndef ST7735S_BAND_ROWS
#define ST7735S_BAND_ROWS 40
#endif

#ifdef __cplusplus
extern "C" {
#endif

int SPI_Init_ST7735(void);
void Pin_CS_Low(void);
void Pin_CS_High(void);
void Pin_RES_Active(void);
void Pin_RES_Inactive(void);
void Pin_DC_High(void);
void Pin_DC_Low(void);
void Pin_BLK_Pct(uint8_t);

/**
 * @brief Bench-validation debug helper: pulses DISP_RESET (MCP23008 GP7)
 * directly, standalone from SPI_Init_ST7735()/ST7735S_Init()'s full
 * sequence — for confirming the MCP23008 pin toggles electrically (scope
 * probe, etc.) without re-running the whole display bring-up. Same spirit
 * as power_debug_hold_vbat_div(). Safe to call even if the display hasn't
 * been initialized yet.
 */
void ST7735S_hard_reset_pulse(void);

void SPI_send(uint16_t len, uint8_t *data);
void SPI_TransmitCmd(uint16_t len, uint8_t *data);
void SPI_TransmitData(uint16_t len, uint8_t *data);
void SPI_Transmit(uint16_t len, uint8_t *data);

/* Backlight level */
extern uint8_t backlight_pct;

#ifdef __cplusplus
} // extern "C"
#endif
#endif
