//*****************************************************************************
//!
//! @File BQ25120A.c
//! @author Anders Bandt
//! @brief This file contains functions for IC control of the BQ25120A
//! @version 0.9
//! @date September 2023
//!
//*****************************************************************************

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//! -----------------------------------------------------------------------------------------------------------------------//
//! HEADER FILES ----------------------------------------------------------------------------------------------------------//
//! -----------------------------------------------------------------------------------------------------------------------//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/* Standard C99 stuff */
#include "BQ25120A.h"
#include <stdlib.h>
#include <unistd.h>


/* Driver Header files  */
#include <config/ti_drivers_config.h>
#include <ti/drivers/GPIO.h>
#include <ti/display/Display.h>

/* My header files  */
#include <src/comm/i2c.h>
#include <src/comm/i2c_addr.h>
#include <src/ic/bms/BQ25120A.h>
#include <src/hardware/led.h>



// TODO: (general) I should add some hardware abstraction layer `bms.c`


/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//! -----------------------------------------------------------------------------------------------------------------------//
//! LOCAL FUNCTIONS -------------------------------------------------------------------------------------------------------//
//! -----------------------------------------------------------------------------------------------------------------------//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


/**
 * bq25120A_init_all: initializes the registers of the BQ25120A battery charger IC
 */
void bq25120a_init_all(Display_Handle display)
{
    i2c_write(BMS_I2C_ADDR, BQ25120A_STAT0, 0x00, display); // DEFAULT (status and ship mode register)
    i2c_write(BMS_I2C_ADDR, BQ25120A_MASK0, 0x00, display); // DEFAULT (faults and faults mask)
    i2c_write(BMS_I2C_ADDR, BQ25120A_MASK1, 0x28, display); // DEFAULT (TS control and faults Masks)

    i2c_write(BMS_I2C_ADDR, BQ25120A_ICH, 0xAC, display); // ISET=150mA, charger=enabled
    //    i2c_write(BMS_I2C_ADDR, BQ25120A_ICH, 0xAE, display); // ISET=150mA, charger=disabled

    i2c_write(BMS_I2C_ADDR, BQ25120A_PRETRM, 0x8A, display); // ITERM=8 mA,
    i2c_write(BMS_I2C_ADDR, BQ25120A_VBATCTRL, 0x78, display); // VBATREG=4.2 V

    // SYSVOUT - output voltage on VDDS
    i2c_write(BMS_I2C_ADDR, BQ25120A_SYSVOUT, 0xBA, display); // 2.6V (according to register config tool)

    //    i2c_write(BMS_I2C_ADDR, BQ25120A_LSLDO, 0x7C, display); // DEFAULT (Load Switch, LS/LDO=disabled)
    i2c_write(BMS_I2C_ADDR, BQ25120A_PUSHBUTTON, 0x60, display); // DEFAULT (MRWAKE1=0-80ms, MRREC=Hi-Z, MR=0-5s, output=PG)

    //    i2c_write(BMS_I2C_ADDR, BQ25120A_ILIM, 0x3B, display); // BUVLO = 2.8V, ILIM=400mA
    i2c_write(BMS_I2C_ADDR, BQ25120A_ILIM, 0x33, display); // BUVLO = 2.8V, ILIM=300mA

    i2c_write(BMS_I2C_ADDR, BQ25120A_VINDPM, 0x44, display); // DEFAULT (VINDPM=4.6V, safety timer=9 hour fast charge)
    return;
}

void bq25120a_mask_faults(void) {
    i2c_write(BMS_I2C_ADDR, BQ25120A_STAT0, 0x00, NULL); // DEFAULT (status and ship mode register)
    i2c_write(BMS_I2C_ADDR, BQ25120A_MASK0, 0x00, NULL); // DEFAULT (faults and faults mask)
    i2c_write(BMS_I2C_ADDR, BQ25120A_MASK1, 0x28, NULL); // DEFAULT (TS control and faults Masks)
}




/**
 * bq25120A_sysvout: adjusts the settings for SYSVOUT
 */
void bq25120a_sysvout(uint8_t vset, Display_Handle display) {
    uint8_t reg_val = 0;

    reg_val |= SYS_VOUT_ENABLE_MASK;
    reg_val |= SYS_SEL_1p5_TO_2p75_MASK;
    reg_val |= Sys_vout_2p6;
    i2c_write(BMS_I2C_ADDR, BQ25120A_SYSVOUT, reg_val, display);
}


/**
 * bq25120A_sweep: sweeps the SYSVOUT (VDDS) voltage on the BQ25120A
 */
void bq25120a_sysvout_sweep(Display_Handle display) {
    // Initialize reg_val with 0 (assuming it's a variable of an appropriate type)
    uint8_t reg_val = 0;

    int y;
    int z;
    for(y=0; y < 4; y++) {
        for(z=0; z <= 0xF; z++) {
            reg_val = 0;
            reg_val |= (1 << 7); // enable=1
            reg_val |= (y << 5); // 3=11,2=10,1=01,0=00
            reg_val |= (z << 1); // 0xF is binary 1111

            i2c_write(BMS_I2C_ADDR, BQ25120A_SYSVOUT, reg_val, display);
            sleep(1);
        }
    }
}


/*
 * bq25120A_reset: write 1 to register address 0x09 to reset all registers to default values
 */
void bq25120a_reset(Display_Handle display) {
    i2c_write(BMS_I2C_ADDR, BQ25120A_ILIM, 0xAB, display);
}


/*
 * bq25120A_read_all: reads all 12 registers from the BMS and prints out on UART
 */
void bq25120a_read_all(Display_Handle display) {
    uint8_t r_d[12]; // Array to store register values

    // Read the registers
    for (int i = 0; i <= 0xB; i++) {
        i2c_read(BMS_I2C_ADDR, i, &r_d[i], 1, NULL);
    }

    // Print the register values
    Display_printf(display, 0, 0, "\nBMS: %x %x %x %x %x %x %x %x %x %x %x %x\r\n",
                   r_d[0],
                   r_d[1],
                   r_d[2],
                   r_d[3],
                   r_d[4],
                   r_d[5],
                   r_d[6],
                   r_d[7],
                   r_d[8],
                   r_d[9],
                   r_d[10],
                   r_d[11]);
}



void bq25120a_stat_read(Display_Handle display) {
    uint8_t reg_data = 0;
    i2c_read(BMS_I2C_ADDR, BQ25120A_STAT0, &reg_data, 1, NULL);

    // bits 6 and 7 are status
    // NOTE: below logic is untested
    if ((reg_data >> 6) & 0x11) {
        Display_printf(display, 0, 0, "\tBMS FAULT!", reg_data);
    }

    // TODO: could possibly add some other conditions of the status bits to print out
    
    
    // bit 4 is reset condition
    if ((reg_data >> 4 ) & 0x01) {
        Display_printf(display, 0, 0, "\tBMS RESET!", reg_data);
    }

    // bit 2 is VINDPM
    if ((reg_data >> 2 ) & 0x01) {
        Display_printf(display, 0, 0, "\tVIN_DMP active!", reg_data);
    }
}


/*
 * bq25120A_stat_read: reads the status register 0x00 and prints out faults over UART
 */
int bq25120a_fault_cond() {
    uint8_t reg_data = 0;
    i2c_read(BMS_I2C_ADDR, BQ25120A_STAT0, &reg_data, 1, NULL);

    // read bits 6 and 7
    int status = (reg_data & 0b11000000) >> 6;
    
    // PERFORM ACTIONS
    if (status == 3) { // fault condition
        return 1;
    }
    else {
        return 0;
    }
}

// TODO: I need to add something for charge done detection
int bq25120a_charging(void) {
    uint8_t reg = 1;
    i2c_read(BMS_I2C_ADDR, BQ25120A_STAT0, &reg, 1, NULL);

    return (reg & 0b11000000) >> 6;
}

/*
 * bq25120A_fault_read: reads the fault registers at 0x01 and 0x02 and prints out results over UART
 */
int bq25120a_fault_read(Display_Handle display) {
    uint8_t reg_data = 0;

    // read the status and ship mode register
    i2c_read(BMS_I2C_ADDR, BQ25120A_STAT0, &reg_data, 1, NULL);

    // bit 4 is reset condition
    if ((reg_data >> 4 ) & 0x01) {
        Display_printf(display, 0, 0, "\tBMS_RESET fault", reg_data);
        return 1;
    }

    // bit 4 is reset condition
    if ((reg_data >> 3 ) & 0x01) {
        Display_printf(display, 0, 0, "\tBMS_TIMER fault", reg_data);
        return 2;
    }


    // read faults and faults mask register
    i2c_read(BMS_I2C_ADDR, BQ25120A_MASK0, &reg_data, 1, NULL);

    // bit 7 is VIN_OV fault
    if ((reg_data >> 7 ) & 0x01) {
        Display_printf(display, 0, 0, "\t\tVIN_OV fault!");
        return 3;
    }

    // bit 6 is VIN_UV fault
    if ((reg_data >> 6 ) & 0x01) {
        Display_printf(display, 0, 0, "\t\tVIN_UV fault!");
        return 4;
    }

    // bit 5 is BAT_UVLO fault
    if ((reg_data >> 5 ) & 0x01) {
        Display_printf(display, 0, 0, "\t\tBAT_UVLO fault!");
        return 5;
    }


    // READ TS control and MASKS fault registers
    i2c_read(BMS_I2C_ADDR, BQ25120A_MASK1, &reg_data, 1,  NULL);

    // bits 6 and 5 are for TS Fault mode
    uint8_t ts_fault = ((reg_data >> 5) & 0x03);
    if (ts_fault) {
        Display_printf(display, 0, 0, "\t\tTS fault!");
        switch(ts_fault)
            {
            case 1:
                Display_printf(display, 0, 0, "\t\t\ttemp < TCOLD or temp > THOT. Charging suspended.");
                break;
            case 2:
                Display_printf(display, 0, 0, "\t\t\tTCOOL > temp > TCOLD. Charging current reduced by half.");
                break;
            case 3:
                Display_printf(display, 0, 0, "\t\t\tTWARM < temp < THOT. Charging voltage reduced by 140 mV.");
                break;
            }
        return 6;
    }

    // only return 99 if we didn't find any other fault condition
    return 99;
}


void bq25120a_vindpm_timer_read(Display_Handle display) {
    uint8_t reg_data = 0;
    i2c_read(BMS_I2C_ADDR, BQ25120A_PUSHBUTTON, &reg_data, 1, display);
    i2c_read(BMS_I2C_ADDR, BQ25120A_VINDPM, &reg_data, 1,  display);
    Display_printf(display, 0, 0, "register=%x", reg_data);
}

/*
 * pg=0, output of PG is PG
 * pg=1, output is voltage shifted push-button MR input
 */ 
void bq25120a_PGB_MR(int pg) {
    uint8_t reg = 0;
    i2c_read(BMS_I2C_ADDR, BQ25120A_PUSHBUTTON, &reg, 1, NULL);
    reg &= !(PUSHBUTTON_PGB_MR_MASK);
    i2c_write(BMS_I2C_ADDR, BQ25120A_PUSHBUTTON, reg, NULL);
}

/*
 * bq25120A_write_CD: writes a certain state to the CD pin on the BMS
 */

// TODO: how do I ensure the default CD state is high?
void bq25120a_write_CD(int state) {
    GPIO_write(CONFIG_GPIO_BMS_CD, state);
}

/*
 * bq25120A_toggle_CD: toggles the CD pin on the BMS, and leaves it high
 */
void bq25120a_toggle_CD(void) {
    GPIO_write(CONFIG_GPIO_BMS_CD, 1);
    usleep(50*100);
    GPIO_write(CONFIG_GPIO_BMS_CD, 0);
    usleep(50*100);
    GPIO_write(CONFIG_GPIO_BMS_CD, 1);
}

/*
 *bq25120A_vbmon_read: reads the voltage based battery monitor register
 */
int bq25120a_vbmon_read(int new_read, Display_Handle display) {
    uint8_t reg = 0;
    i2c_read(BMS_I2C_ADDR, BQ25120A_VBATSTAT, &reg, 1, display);

    // if we want to initiate a new VBATREG reading
    if (new_read) {
        i2c_write(BMS_I2C_ADDR, BQ25120A_VBATSTAT, (1 << 7), display);
    }

    uint8_t vbmon_range = (reg >> 5) & 0b11;
    uint8_t vbmon_th = (reg >> 2) & 0b111;

    int vbmon_range_per = 0;
    int vbmon_th_per = 0;


    // perform mapping on VMON_RANGE
    switch(vbmon_range)
        {
        case 0b11:
            vbmon_range_per = 90;
            break;
        case 0b10:
            vbmon_range_per = 80;
            break;
        case 0b01:
            vbmon_range_per = 70;
            break;
        case 0b00:
            vbmon_range_per = 60;
            break;
        }


    // perform mapping on VBMON_TH
    switch(vbmon_th)
        {
        case 0b111:
            vbmon_th_per = 8;
            break;
        case 0b110:
            vbmon_th_per = 6;
            break;
        case 0b011:
            vbmon_th_per = 4;
            break;
        case 0b010:
            vbmon_th_per = 2;
            break;
        case 0b001:
            vbmon_th_per = 0;
            break;
        }

    // TODO: I should try adding some scaling here to scale
    // from 60 to 98 to 0 to 100

    return vbmon_range_per + vbmon_th_per;
}


