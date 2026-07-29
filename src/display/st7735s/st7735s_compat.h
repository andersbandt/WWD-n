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
