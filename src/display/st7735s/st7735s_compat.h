#ifndef __st7735s_compat_h__
#define __st7735s_compat_h__

#include <inttypes.h>

/* this may differ from the default 80x160 */
#define defWIDTH   128
#define defHEIGHT  180
#define defXSTART  0
#define defYSTART  0

/* BUFFER: full frame buffer. Don't think it will fit into nRF52832 RAM*/
// #define BUFFER

/* BUFFER1: slowest, used for limited RAM */
#define BUFFER1

/*HVBUFFER: takes advantage of writing adjacent same color pixels*/
// #define HVBUFFER

#ifdef __cplusplus
extern "C" {
#endif

void SPI_Init_ST7735(void);
void Pin_CS_Low(void);
void Pin_CS_High(void);
void Pin_RES_High(void);
void Pin_RES_Low(void);
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
