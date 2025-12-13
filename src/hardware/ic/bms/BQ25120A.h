//*****************************************************************************
//!
//! @file BQ25120A.h
//! @author Anders Bandt
//! @brief
//! @version 0.9
//! @date September 2023
//!
//*****************************************************************************

#ifndef SRC_IC_BQ25120A_H_
#define SRC_IC_BQ25120A_H_


#include <ti/display/Display.h>


// define I2C address
#define BQ25120A_ADDR 0x6A

// define register addresses in BMS
#define BQ25120A_STAT0           (0x00)
#define BQ25120A_MASK0           (0x01)
#define BQ25120A_MASK1           (0x02)
#define BQ25120A_ICH             (0x03)
#define BQ25120A_PRETRM          (0x04)
#define BQ25120A_VBATCTRL        (0x05)
#define BQ25120A_SYSVOUT         (0x06)
#define BQ25120A_LSLDO           (0x07)
#define BQ25120A_PUSHBUTTON      (0x08)
#define BQ25120A_ILIM            (0x09)
#define BQ25120A_VBATSTAT        (0x0A)
#define BQ25120A_VINDPM          (0x0B)


// 6,5=SYS_SEL
// 4-1=SYS_VOUT

// Setting bit 7 (1=enable, 0=disable SYS_VOUT)
#define SYS_VOUT_ENABLE_MASK               0 << 7
#define SYS_VOUT_DISABLE_MASK              1 << 7

// Setting bits 5 and 6 (output voltage selection)
//  0/00 = 1.1V and 1.2 V selection
//  1/01 = 1.3V through 2.8 V selection
//  2/10 = 1.5V through 2.75 V selection
//  3/11 - 1.8 v through 3.3 V selection
#define SYS_SEL_1p1_TO_1p2_MASK           00 << 5
#define SYS_SEL_1p3_TO_2p8_MASK           01 << 5
#define SYS_SEL_1p5_TO_2p75_MASK          02 << 5
#define SYS_SEL_1p8_TO_3p3_MASK           03 << 5

// TODO: is there a way to add SYS_VOUT to my register masks?
enum SYS_VOUT {Sys_vout_2p6 = 0xF};

// define register bitmasks
#define PUSHBUTTON_PGB_MR_MASK   0x04


// register initialization function
void bq25120a_init_all(Display_Handle display);



void bq25120a_mask_faults(void);


// adjust the output voltage
void bq25120a_sysvout(uint8_t vset, Display_Handle display);


// sweeps the output voltage
void bq25120a_sysvout_sweep(Display_Handle display);


// attempts to perform a rest on the bq25120a
void bq25120a_reset(Display_Handle display);


/**
 * @brief performs a read on register 0x00
 */
void bq25120a_stat_read(Display_Handle display);


/**
 * @brief returns if there is a fault condition on the bms
 */
int bq25120a_fault_cond();


/**
 * @brief returns the charging status
 */
int bq25120a_charging(void);


/**
 * @brief reads BMS fault registers
 * @return the status code as int as defined in bms.c
 */
int bq25120a_fault_read(Display_Handle display);


/**
 * @brief reads the VIN_DPM and timers register
 */
void bq25120a_vindpm_timer_read(Display_Handle display);


/**
 * @brief reads every register on bq25120a
 */
void bq25120a_read_all(Display_Handle display);


/**
 * @brief supposed to set the PG and MR behavior
 */
void bq25120a_PGB_MR(int pg);


/**
 * @brief sets the CD pin to a certain state through GPIO
 */
void bq25120a_write_CD(int state);


/**
 * @brief toggles the CD pin on the BMS through GPIO
 */
void bq25120a_toggle_CD(void);


/**
 * @brief conducts reading of VBMON register
 */
int bq25120a_vbmon_read(int new_read, Display_Handle display);


#endif /* SRC_IC_bq25120a_H_ */
