//*****************************************************************************
//!
//! @file ICM_42670.c
//! @author Anders Bandt
//! @brief Anders' IMU (ICM_42670) control file
//! @version 0.9
//! @date November 2023
//!
//*****************************************************************************

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//! -----------------------------------------------------------------------------------------------------------------------//
//! HEADER FILES ----------------------------------------------------------------------------------------------------------//
//! -----------------------------------------------------------------------------------------------------------------------//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/* Standard C99 stuff */
#include <stdint.h>
#include <stdbool.h>


/* Zephyr header files*/
#include <zephyr/drivers/spi.h>
#include <zephyr/logging/log.h>

/* My header files  */
//#include <src/comm/comm.h>
#include <circular_buffer.h>


/* IMU header files  */
#include <imu.h>
#include <ICM_42670.h>
#include <inv_imu_driver.h>
#include <inv_imu_apex.h>
#include <inv_imu_defs.h> // added by Anders so I can do some register read verification


LOG_MODULE_REGISTER(ICM_42670, CONFIG_LOG_DEFAULT_LEVEL);


#define INV_IMU_WHOAMI   0x67


//extern Circular_Buffer * imu_data_buffer;


// This is used by the event callback (not object aware), declared static
static inv_imu_sensor_event_t *event;


// declare ICM device driver and other C++ variables
struct inv_imu_device icm_driver;

// declare other various local variables
uint32_t step_cnt_ovflw;
uint8_t int_status3;
bool apex_tilt_enable;
bool apex_pedometer_enable;


/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//! -----------------------------------------------------------------------------------------------------------------------//
//! SPI and EVENT FUNCTIONS -------------------------------------------------------------------------------------------------------------//
//! -----------------------------------------------------------------------------------------------------------------------//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/*
 * readIMUReg: I created this function to abstract the one in the driver
 */
int readIMUReg(int reg) {
    int rc = 0;
    uint8_t data;

    rc |= inv_imu_read_reg(&icm_driver, reg, 1, &data);
    if (rc) {
        return -1;
    }
    return data;
}


void dumpIMUReg() {
    int data = 0;
    LOG_INF("IMU_reg,IMU_data");
    for (int reg = 0; reg < 0x13; reg++) {
        data = readIMUReg(reg);
        LOG_INF("0x%x,0x%x", reg,data);
    }
}


void event_print(inv_imu_sensor_event_t *evt) {
    if (isAccelDataValid(evt) && isGyroDataValid(evt)) {
        LOG_DBG("x-y-z-temp-timestamp: %d,%d,%d,%d,%u",
                       evt->accel[0],
                       evt->accel[1],
                       evt->accel[2],
                       evt->temperature,
                       evt->timestamp_fsync);
    }
    else {
        LOG_DBG("Data invalid");
    }
}


/*
 * event_cb: callback for new event
 */
void event_cb(inv_imu_sensor_event_t *evt) {
    circular_buffer_add(imu_data_buffer, evt);
    imu_set_latest_event(evt);
}


/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//! -----------------------------------------------------------------------------------------------------------------------//
//! FUNCTIONS -------------------------------------------------------------------------------------------------------------//
//! -----------------------------------------------------------------------------------------------------------------------//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/*
 * init_icm: initializes the IMU. Sets some needed serial interface parameters and calls the driver init function
 */
int init_icm() {
    struct inv_imu_serif icm_serif;
    int rc = 0;
    uint8_t who_am_i;

    icm_serif.serif_type = UI_SPI4;
    icm_serif.read_reg = imu_spi_read;
    icm_serif.write_reg = imu_spi_write;


    // Initialize serial interface between MCU and ICM-42670
    icm_serif.context = NULL; // used to be equal to (void*)this, but that will not work beacause it is no longer a class
    icm_serif.max_read  = 2048; /* maximum number of bytes allowed per serial read */
    icm_serif.max_write = 2048; /* maximum number of bytes allowed per serial write */

    icm_driver.sensor_event_cb = event_cb;

    rc = inv_imu_init(&icm_driver, &icm_serif, event_cb);
    if (rc != INV_ERROR_SUCCESS) {
        LOG_ERR("Error with IMU initialization, got status code [%d]", rc);
        return rc;
    }

    // /* Check WHOAMI */
    rc = inv_imu_get_who_am_i(&icm_driver, &who_am_i);
    if (rc != 0) {
        return -2;
    }

    if (who_am_i != INV_IMU_WHOAMI) {
        return -3;
    }

    LOG_INF("\tgot IMU WHOAMI: [0x%x]", who_am_i);    
    return 0;
}



ACCEL_CONFIG0_FS_SEL_t accel_fsr_g_to_param(uint16_t accel_fsr_g) {
  ACCEL_CONFIG0_FS_SEL_t ret = ACCEL_CONFIG0_FS_SEL_16g;

  switch(accel_fsr_g) {
  case 2:  ret = ACCEL_CONFIG0_FS_SEL_2g;  break;
  case 4:  ret = ACCEL_CONFIG0_FS_SEL_4g;  break;
  case 8:  ret = ACCEL_CONFIG0_FS_SEL_8g;  break;
  case 16: ret = ACCEL_CONFIG0_FS_SEL_16g; break;
  default:
    /* Unknown accel FSR. Set to default 16G */
    break;
  }
  return ret;
}


GYRO_CONFIG0_FS_SEL_t gyro_fsr_dps_to_param(uint16_t gyro_fsr_dps) {
  GYRO_CONFIG0_FS_SEL_t ret = GYRO_CONFIG0_FS_SEL_2000dps;

  switch(gyro_fsr_dps) {
  case 250:  ret = GYRO_CONFIG0_FS_SEL_250dps;  break;
  case 500:  ret = GYRO_CONFIG0_FS_SEL_500dps;  break;
  case 1000: ret = GYRO_CONFIG0_FS_SEL_1000dps; break;
  case 2000: ret = GYRO_CONFIG0_FS_SEL_2000dps; break;
  default:
    /* Unknown gyro FSR. Set to default 2000dps" */
    break;
  }
  return ret;
}


ACCEL_CONFIG0_ODR_t accel_freq_to_param(uint16_t accel_freq_hz) {
  ACCEL_CONFIG0_ODR_t ret = ACCEL_CONFIG0_ODR_100_HZ;

  switch(accel_freq_hz) {
  case 12:   ret = ACCEL_CONFIG0_ODR_12_5_HZ;  break;
  case 25:   ret = ACCEL_CONFIG0_ODR_25_HZ;  break;
  case 50:   ret = ACCEL_CONFIG0_ODR_50_HZ;  break;
  case 100:  ret = ACCEL_CONFIG0_ODR_100_HZ; break;
  case 200:  ret = ACCEL_CONFIG0_ODR_200_HZ; break;
  case 400:  ret = ACCEL_CONFIG0_ODR_400_HZ; break;
  case 800:  ret = ACCEL_CONFIG0_ODR_800_HZ; break;
  case 1600: ret = ACCEL_CONFIG0_ODR_1600_HZ;  break;
  default:
    /* Unknown accel frequency. Set to default 100Hz */
    break;
  }
  return ret;
}


GYRO_CONFIG0_ODR_t gyro_freq_to_param(uint16_t gyro_freq_hz) {
  GYRO_CONFIG0_ODR_t ret = GYRO_CONFIG0_ODR_100_HZ;

  switch(gyro_freq_hz) {
  case 12:   ret = GYRO_CONFIG0_ODR_12_5_HZ;  break;
  case 25:   ret = GYRO_CONFIG0_ODR_25_HZ;  break;
  case 50:   ret = GYRO_CONFIG0_ODR_50_HZ;  break;
  case 100:  ret = GYRO_CONFIG0_ODR_100_HZ; break;
  case 200:  ret = GYRO_CONFIG0_ODR_200_HZ; break;
  case 400:  ret = GYRO_CONFIG0_ODR_400_HZ; break;
  case 800:  ret = GYRO_CONFIG0_ODR_800_HZ; break;
  case 1600: ret = GYRO_CONFIG0_ODR_1600_HZ;  break;
  default:
    /* Unknown gyro ODR. Set to default 100Hz */
    break;
  }
  return ret;
}



// DEBUG function to check on interrupt configs for the IMU
void checkInterruptIMU() {
    int status = 0;
    inv_imu_interrupt_parameter_t config_int = { (inv_imu_interrupt_value)0 };

    // check interrupt 1
    status |= inv_imu_get_config_int1(&icm_driver, &config_int);
    LOG_INF("Interrupt 1.");
    LOG_INF("  INV_UI_FSYNC: %d", config_int.INV_UI_FSYNC);
    LOG_INF("  INV_UI_DRDY: %d", config_int.INV_UI_DRDY);
    LOG_INF("  INV_FIFO_THS: %d", config_int.INV_FIFO_THS);
    LOG_INF("  INV_FIFO_FULL: %d", config_int.INV_FIFO_FULL);
    LOG_INF("  INV_SMD: %d", config_int.INV_SMD);
    LOG_INF("  INV_WOM_X: %d", config_int.INV_WOM_X);
    LOG_INF("  INV_WOM_Y: %d", config_int.INV_WOM_Y);
    LOG_INF("  INV_WOM_Z: %d", config_int.INV_WOM_Z);
    LOG_INF("  INV_FF: %d", config_int.INV_FF);
    LOG_INF("  INV_LOWG: %d", config_int.INV_LOWG);
    LOG_INF("  INV_STEP_DET: %d", config_int.INV_STEP_DET);
    LOG_INF("  INV_STEP_CNT_OVFL: %d", config_int.INV_STEP_CNT_OVFL);
    LOG_INF("  INV_TILT_DET: %d", config_int.INV_TILT_DET);

    // check interrupt 2
    status |= inv_imu_get_config_int2(&icm_driver, &config_int);
    LOG_INF("Interrupt 2.");
    LOG_INF("  INV_UI_FSYNC: %d", config_int.INV_UI_FSYNC);
    LOG_INF("  INV_UI_DRDY: %d", config_int.INV_UI_DRDY);
    LOG_INF("  INV_FIFO_THS: %d", config_int.INV_FIFO_THS);
    LOG_INF("  INV_FIFO_FULL: %d", config_int.INV_FIFO_FULL);
    LOG_INF("  INV_SMD: %d", config_int.INV_SMD);
    LOG_INF("  INV_WOM_X: %d", config_int.INV_WOM_X);
    LOG_INF("  INV_WOM_Y: %d", config_int.INV_WOM_Y);
    LOG_INF("  INV_WOM_Z: %d", config_int.INV_WOM_Z);
    LOG_INF("  INV_FF: %d", config_int.INV_FF);
    LOG_INF("  INV_LOWG: %d", config_int.INV_LOWG);
    LOG_INF("  INV_STEP_DET: %d", config_int.INV_STEP_DET);
    LOG_INF("  INV_STEP_CNT_OVFL: %d", config_int.INV_STEP_CNT_OVFL);
    LOG_INF("  INV_TILT_DET: %d", config_int.INV_TILT_DET);

    // check the actual interrupt status register
    LOG_INF("Interrupt registers:");
    uint8_t reg_data = readIMUReg(INT_STATUS);
    LOG_INF("  INT_STATUS = [0x%x]", reg_data);
    reg_data = readIMUReg(INT_CONFIG);
    LOG_INF("  INT_CONFIG[0x%x] = 0x%x", INT_CONFIG, reg_data);
    reg_data = readIMUReg(INT_CONFIG0_MREG1);
    LOG_INF("  INT_CONFIG0[0x%x] = 0x%x", INT_CONFIG0_MREG1, reg_data);
    reg_data = readIMUReg(INT_CONFIG1_MREG1);
    LOG_INF("  INT_CONFIG1[0x%x] = 0x%x", INT_CONFIG1_MREG1, reg_data);
}


/*
 * startAccel: starts the accelerometer
 */
int startAccel(uint16_t odr, uint16_t fsr) {
  int rc = 0;
  rc |= inv_imu_set_accel_fsr(&icm_driver, accel_fsr_g_to_param(fsr));
  rc |= inv_imu_set_accel_frequency(&icm_driver, accel_freq_to_param(odr));
  rc |= inv_imu_enable_accel_low_noise_mode(&icm_driver);
  return rc;
}


/*
 * startGyro: starts the gyrometer
 */
int startGyro(uint16_t odr, uint16_t fsr) {
  int rc = 0;
  rc |= inv_imu_set_gyro_fsr(&icm_driver, gyro_fsr_dps_to_param(fsr));
  rc |= inv_imu_set_gyro_frequency(&icm_driver, gyro_freq_to_param(odr));
  rc |= inv_imu_enable_gyro_low_noise_mode(&icm_driver);
  return rc;
}


/*
 * Data retrieval function. Reads from FIFO
 *
 * NOTE: this function requires the interrupt pin be defined elsewhere at the hardware level
 */
int enableFifoInterrupt(uint8_t fifo_watermark) {
    int rc = 0;
    uint8_t data;

    // configure FIFO and write watermark level
    rc |= inv_imu_configure_fifo(&icm_driver, INV_IMU_FIFO_ENABLED);
    rc |= inv_imu_write_reg(&icm_driver, FIFO_CONFIG2, 1, &fifo_watermark);

    /* 2026-07-31: was counter == threshold (WM_GT_TH_EN cleared) — fragile,
     * since if FIFO_COUNT ever skips past exactly fifo_watermark between
     * checks the condition is never met again until it wraps. Confirmed live
     * on hardware: imu_init() succeeds, INT1 is armed, but INT_STATUS's
     * FIFO_THS bit never latched and the GPIO edge-interrupt counter stayed
     * at 0 after 8+ seconds at 100Hz — the exact-match condition was
     * (plausibly) never being hit. Switched to >= threshold, which is also
     * what the datasheet's own FIFO_CONFIG5 description favors. */
    rc |= inv_imu_read_reg(&icm_driver, FIFO_CONFIG5_MREG1, 1, &data);
    data |= (uint8_t)FIFO_CONFIG5_WM_GT_TH_EN;
    rc |= inv_imu_write_reg(&icm_driver, FIFO_CONFIG5_MREG1, 1, &data);

    /* Route the FIFO watermark condition to the INT1 pin.
     *
     * Without this the watermark only ever sets a bit in INT_STATUS — the
     * physical pin never moves, so no MCU interrupt can fire. The old board
     * carried FIFO_THS on INT2, but INT2 is not wired on the nRF52833 hardware
     * (P0.09/INT1 is the only interrupt line), so it has to go to INT1 here.
     *
     * WOM_X/Y/Z share this same pin (2026-08-22, raise-to-wake v1 — see
     * imu_notes.md). This is safe to share because the two conditions land in
     * different status registers (FIFO cause in INT_STATUS, WOM cause in
     * INT_STATUS2) and each has exactly one reader: get_fifo_data() ->
     * inv_imu_get_data_from_fifo() owns INT_STATUS, button_handler_thread_entry()
     * (main.c) owns INT_STATUS2. Nothing else reads either register, so there's
     * no clear-on-read race. WOM itself isn't actually armed until
     * inv_imu_enable_wom() runs in startApex() below — enabling the routing here
     * just means it's ready the moment WOM_CONFIG's enable bit goes live. Tilt
     * (INT_STATUS3) is deliberately NOT added here: that register already has an
     * exclusive reader (updateApex(), polled every 9s from sensor_update_thread)
     * and adding a second one from the interrupt thread would race it. */
    {
        inv_imu_interrupt_parameter_t it = { 0 };  /* all sources off... */

        it.INV_FIFO_THS = INV_IMU_ENABLE;          /* ...except the watermark */
        it.INV_WOM_X    = INV_IMU_ENABLE;          /* ...and WOM (raise-to-wake v1) */
        it.INV_WOM_Y    = INV_IMU_ENABLE;
        it.INV_WOM_Z    = INV_IMU_ENABLE;
        rc |= inv_imu_set_config_int1(&icm_driver, &it);
    }

    /* Electrical config for INT1. Push-pull is required (the pin is not pulled
     * on this board), and the polarity must match the DTS, which declares
     * int-gpios as GPIO_ACTIVE_LOW. Pulsed rather than latched so each watermark
     * crossing produces one clean edge instead of a level held until INT_STATUS
     * is read. */
    rc |= inv_imu_read_reg(&icm_driver, INT_CONFIG, 1, &data);
    data &= (uint8_t)~(INT_CONFIG_INT1_MODE_MASK |
                       INT_CONFIG_INT1_DRIVE_CIRCUIT_MASK |
                       INT_CONFIG_INT1_POLARITY_MASK);
    data |= (uint8_t)INT_CONFIG_INT1_MODE_PULSED;
    data |= (uint8_t)INT_CONFIG_INT1_DRIVE_CIRCUIT_PP;
    data |= (uint8_t)INT_CONFIG_INT1_POLARITY_LOW;
    rc |= inv_imu_write_reg(&icm_driver, INT_CONFIG, 1, &data);
    LOG_INF("INT1 configured for FIFO_THS + WOM: INT_CONFIG = 0x%02x", data);


    // do some Ders verification
    LOG_DBG("Printing out some critical IMU FIFO registers ...");
    int reg_data = readIMUReg(INTF_CONFIG0);
    LOG_DBG("\tINTF_CONFIG0[0x%x] = 0x%x", INTF_CONFIG0, reg_data);
    reg_data = readIMUReg(FIFO_CONFIG1);
    LOG_DBG("\tFIFO_CONFIG1[0x%x] = 0x%x", FIFO_CONFIG1, reg_data);
    reg_data = readIMUReg(FIFO_CONFIG2);
    LOG_DBG("\tFIFO_CONFIG2[0x%x] = 0x%x", FIFO_CONFIG2, reg_data);
    reg_data = readIMUReg(FIFO_CONFIG3);
    LOG_DBG("\tFIFO_CONFIG3[0x%x] = 0x%x", FIFO_CONFIG3, reg_data);

    // read some registers from the MREG1
    reg_data = readIMUReg(TMST_CONFIG1_MREG1);
    LOG_DBG("\tTMST_CONFIG1[0x%x] = 0x%x", TMST_CONFIG1_MREG1, reg_data);
    reg_data = readIMUReg(FIFO_CONFIG5_MREG1);
    LOG_DBG("\tFIFO_CONFIG5[0x%x] = 0x%x", FIFO_CONFIG5_MREG1, reg_data);
    reg_data = readIMUReg(FIFO_CONFIG6_MREG1);
    LOG_DBG("\tFIFO_CONFIG6[0x%x] = 0x%x", FIFO_CONFIG6_MREG1, reg_data);
    reg_data = readIMUReg(SENSOR_CONFIG3_MREG1);
    LOG_DBG("\tSENSOR_CONFIG3_MREG1[0x%x] = 0x%x", SENSOR_CONFIG3_MREG1, reg_data);

    // print out final interrupt configuration
    /* checkInterruptIMU(display); */

   return rc;
}


int startApex() {
    int rc = 0;
    inv_imu_apex_parameters_t apex_inputs;

    /* Deliberately NOT forcing accel into LP mode or overwriting its ODR here
     * (this used to hardcode both to 50Hz/low-power, silently undoing
     * whatever startAccel() configured — see imu_notes.md). The only real
     * APEX/DMP constraint is accel_ODR >= DMP_ODR (datasheet + inv_imu_apex.h),
     * so leave accel running at whatever startAccel() set (100Hz, low-noise
     * mode, for FIFO data quality) and only configure the DMP's own rate. */

    /* Disable APEX like features before enabling them */
    rc |= inv_imu_apex_disable_pedometer(&icm_driver);
    rc |= inv_imu_apex_disable_tilt(&icm_driver);
    rc |= inv_imu_disable_wom(&icm_driver);

    rc |= inv_imu_apex_set_frequency(&icm_driver, APEX_CONFIG1_DMP_ODR_50Hz);

    /* Set APEX parameters */
    rc |= inv_imu_apex_init_parameters_struct(&icm_driver, &apex_inputs);
    apex_inputs.power_save =APEX_CONFIG0_DMP_POWER_SAVE_DIS;
    rc |= inv_imu_apex_configure_parameters(&icm_driver, &apex_inputs);

    // ENABLE CERTAIN APEX FEATURES
    rc |= inv_imu_apex_enable_tilt(&icm_driver);
    rc |= inv_imu_apex_enable_pedometer(&icm_driver);
    rc |= inv_imu_configure_wom(&icm_driver, 80, 80, 80,
                                WOM_CONFIG_WOM_INT_MODE_ORED,
                                WOM_CONFIG_WOM_INT_DUR_3_SMPL);
    rc |= inv_imu_enable_wom(&icm_driver);

    // do some Ders verification
    LOG_DBG("Printing out some critical IMU APEX registers ...");
    int reg_data = readIMUReg(APEX_CONFIG0);
    LOG_DBG("\tAPEX_CONFIG0[0x%x] = 0x%x", APEX_CONFIG0, reg_data);
    reg_data = readIMUReg(APEX_CONFIG1);
    LOG_DBG("\tAPEX_CONFIG1[0x%x] = 0x%x", APEX_CONFIG1, reg_data);
    reg_data = readIMUReg(SENSOR_CONFIG3_MREG1);
    LOG_DBG("\tSENSOR_CONFIG3_MREG1[0x%x] = 0x%x - should be 0x00 for APEX", SENSOR_CONFIG3_MREG1, reg_data);
    reg_data = readIMUReg(WOM_CONFIG);
    LOG_DBG("\tWOM_CONFIG[0x%x] = 0x%x", WOM_CONFIG, reg_data);

    // print out final interrupt configuration
    checkInterruptIMU();

    return rc;
}


/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//! -----------------------------------------------------------------------------------------------------------------------//
//! DATA GETTER FUNCTIONS -------------------------------------------------------------------------------------------------//
//! -----------------------------------------------------------------------------------------------------------------------//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/*
 * Data retrieval function. Reads data from temperature registers
 */
int16_t getTempDataFromIMUReg() {
    return inv_imu_get_temp_register(&icm_driver);
}


/*
 * Data retrieval function. Reads data from registers
 */
int getDataFromIMUReg(inv_imu_sensor_event_t* evt) {
    if (evt != NULL) {
        // Set event buffer to be used by the callback --> not sure 100% what this thing is actually doing
        event = evt;

        return inv_imu_get_data_from_registers(&icm_driver);
    }
    else {
        return -1;
    }
}


void getFifoCount() {
        int data1 = readIMUReg(FIFO_COUNTL);
        int data2 = readIMUReg(FIFO_COUNTH);
        LOG_INF("FIFO count (high, low) --> (%d, %d)", data1, data2);
}


/*
 * checkWom: raise-to-wake v1 (2026-08-22, see imu_notes.md) — reads
 * INT_STATUS2 to see whether a WOM event has fired on any axis since the last
 * check. WOM shares INT1 with FIFO_THS (see enableFifoInterrupt()); this
 * function is the exclusive reader of INT_STATUS2, so there's no clear-on-read
 * race with anything else in the driver (contrast with INT_STATUS3/tilt,
 * which already has a reader in updateApex() and is deliberately NOT checked
 * here — same comment). Returns false (not true, not "unknown") on a read
 * error, same best-effort style as the rest of this file's INT_STATUS* checks.
 */
bool checkWom(void) {
    uint8_t status2 = 0;

    if (inv_imu_read_reg(&icm_driver, INT_STATUS2, 1, &status2) != 0) {
        return false;
    }

    return (status2 & (INT_STATUS2_WOM_X_INT_MASK | INT_STATUS2_WOM_Y_INT_MASK |
                        INT_STATUS2_WOM_Z_INT_MASK)) != 0;
}


/*
 *
 */
int getDataFromFifo(void) {
    return inv_imu_get_data_from_fifo(&icm_driver);
}


/*
* updateApex: checks INT_STATUS3 (interrupt status for APEX functions?)
*/
int updateApex(void) {
    int rc = 0;
    uint8_t data;
    rc = inv_imu_read_reg(&icm_driver, INT_STATUS3, 1, &data );
    if (rc == 0) {
        int_status3 |= data;
    }
    return rc;
}


/* Indexed by APEX_DATA3_ACTIVITY_CLASS_t (APEX_DATA3_ACTIVITY_CLASS_MASK = 0x03).
 * Only 0/1/2 are defined by the part; index 3 is reserved/unused but kept so an
 * out-of-range read (should never happen given the mask) can't run off the array. */
static const char *const APEX_ACTIVITY_NAMES[4] = {
    "unknown", // APEX_DATA3_ACTIVITY_CLASS_OTHER
    "walk",    // APEX_DATA3_ACTIVITY_CLASS_WALK
    "run",     // APEX_DATA3_ACTIVITY_CLASS_RUN
    "unknown", // reserved
};


/*
* getPedometer: returns info on the pedometer function of the ICM-42670
*/
int getPedometer(uint32_t * step_count, float * step_cadence, const char **activity) {
    int rc = 0;

    /* Read APEX interrupt status */
    rc |= updateApex();

    // check for overflow
    if (int_status3 & INT_STATUS3_STEP_CNT_OVF_INT_MASK) {
        step_cnt_ovflw++;
        /* Reset pedometer overflow internal status */
        int_status3 &= ~INT_STATUS3_STEP_CNT_OVF_INT_MASK;
    }

    // check for interrupt
    if (int_status3 & (INT_STATUS3_STEP_DET_INT_MASK)) {
        inv_imu_apex_step_activity_t apex_data0;
        float nb_samples           = 0;

        /* Reset pedometer internal status */
        int_status3 &= ~INT_STATUS3_STEP_DET_INT_MASK;

        rc |= inv_imu_apex_get_data_activity(&icm_driver, &apex_data0);
        *step_count = apex_data0.step_cnt + step_cnt_ovflw*(uint32_t)UINT16_MAX;
        /* Converting u6.2 to float */
        nb_samples = (apex_data0.step_cadence >> 2) + (float)(apex_data0.step_cadence & 0x03) * 0.25f;

        // set step cadence
        if (nb_samples != 0) {
            *step_cadence = (float)50 / nb_samples;
        } 
        else {
            step_cadence = 0;
        }

        if (activity != NULL) {
            *activity = APEX_ACTIVITY_NAMES[apex_data0.activity_class & APEX_DATA3_ACTIVITY_CLASS_MASK];
        }
    } 
    else {
        return -11;
    }

    return rc;
}


bool isAccelDataValid(inv_imu_sensor_event_t *evt) {
    return (evt->accel[0] != INVALID_VALUE_FIFO) &&
           (evt->accel[1] != INVALID_VALUE_FIFO) &&
           (evt->accel[2] != INVALID_VALUE_FIFO);
}


bool isGyroDataValid(inv_imu_sensor_event_t *evt) {
#if ICM_IS_GYRO_SUPPORTED
    return (evt->sensor_mask & (1 << INV_SENSOR_GYRO)) != 0;
#else
    return 1;
#endif
}








