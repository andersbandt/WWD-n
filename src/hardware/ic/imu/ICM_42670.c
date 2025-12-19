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


LOG_MODULE_REGISTER(ICM_42670, LOG_LEVEL_INF);


#define INV_IMU_WHOAMI   0x67


//extern Circular_Buffer * imu_data_buffer;


// This is used by the event callback (not object aware), declared static
static inv_imu_sensor_event_t* event;


// declare ICM device driver and other C++ variables
struct inv_imu_device icm_driver;

// declare other various local variables
uint32_t step_cnt_ovflw;
uint8_t int_status3;
bool apex_tilt_enable;
bool apex_pedometer_enable;


#define SPI_DEV DT_COMPAT_GET_ANY_STATUS_OKAY(tdk_icm42670p)
#define SPI_OP SPI_OP_MODE_MASTER | SPI_MODE_CPOL | SPI_WORD_SET(8) | SPI_LINES_SINGLE

static struct spi_dt_spec spi_dev = SPI_DT_SPEC_GET(SPI_DEV, SPI_OP, 0);
// static struct spi_dt_spec spi_dev = NULL;

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//! -----------------------------------------------------------------------------------------------------------------------//
//! SPI and EVENT FUNCTIONS -------------------------------------------------------------------------------------------------------------//
//! -----------------------------------------------------------------------------------------------------------------------//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// TODO: why does this thing take in the serif?
int imu_spi_write(struct inv_imu_serif *serif, uint8_t reg, const uint8_t *buf, uint32_t len) {
    uint8_t tx_data[len+1];
    tx_data[0] = reg;
    memcpy(&tx_data[1], buf, len);

    // Single spi_buf pointing to entire tx_data
    struct spi_buf tx_buf = {
        .buf = tx_data,
        .len = len + 1,
    };

    struct spi_buf_set tx_set = {
        .buffers = &tx_buf,
        .count = 1,
    };

    return spi_transceive_dt(&spi_dev, &tx_set, NULL);
}


int imu_spi_read(struct inv_imu_serif *serif, uint8_t reg, uint8_t *buf, uint32_t len)  {
    struct spi_buf tx_bufs[2] = {
        { .buf = &reg, .len = 1 }, // register address
        { .buf = NULL,   .len = len } // dummy bytes to clock data out
    };

    struct spi_buf_set tx_set = {
        .buffers = tx_bufs,
        .count = 2,
    };

    struct spi_buf rx_bufs[2] = {
        { .buf = NULL,   .len = 1 }, // ignore first byte (register echo)
        { .buf = buf,    .len = len } // store the read data
    };

    struct spi_buf_set rx_set = {
        .buffers = rx_bufs,
        .count = 2,
    };

    return spi_transceive_dt(&spi_dev, &tx_set, &rx_set);
}


void event_print(inv_imu_sensor_event_t *evt) {
    if (isAccelDataValid(evt) && isGyroDataValid(evt)) {
        LOG_INF("x-y-z-temp-timestamp: %d,%d,%d,%d,%d",
                       event->accel[0],
                       event->accel[1],
                       event->accel[2],
                       event->temperature,
                       event->timestamp_fsync);
    }
    else {
        LOG_INF("Data invalid");
    }
}


/*
 * event_cb: callback for new event
 */
void event_cb(inv_imu_sensor_event_t *evt) {
    memcpy(event, evt, sizeof(inv_imu_sensor_event_t)); // TODO: what is this memcpy doing? Nothing right?
    
    /* ADD TO CIRCULAR BUFFER */
    circular_buffer_add(imu_data_buffer, event);
    
    //    bool added = circular_buffer_add(imu_data_buffer, event);
    //    if (!added) {
    //        while (1) {}
    //    }
}


/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//! -----------------------------------------------------------------------------------------------------------------------//
//! FUNCTIONS -------------------------------------------------------------------------------------------------------------//
//! -----------------------------------------------------------------------------------------------------------------------//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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



// DEBUG function to check on interrupt configs for the IMU
void checkInterruptIMU() {
    int status = 0;
    inv_imu_interrupt_parameter_t config_int = { (inv_imu_interrupt_value)0 };

    // check interrupt 1
    status |= inv_imu_get_config_int1(&icm_driver, &config_int);
    LOG_INF("\nInterrupt 1.");
    LOG_INF("INV_UI_FSYNC: %d", config_int.INV_UI_FSYNC);
    LOG_INF("INV_UI_DRDY: %d", config_int.INV_UI_DRDY);
    LOG_INF("INV_FIFO_THS: %d", config_int.INV_FIFO_THS);
    LOG_INF("INV_FIFO_FULL: %d", config_int.INV_FIFO_FULL);
    LOG_INF("INV_SMD: %d", config_int.INV_SMD);
    LOG_INF("INV_WOM_X: %d", config_int.INV_WOM_X);
    LOG_INF("INV_WOM_Y: %d", config_int.INV_WOM_Y);
    LOG_INF("INV_WOM_Z: %d", config_int.INV_WOM_Z);
    LOG_INF("INV_FF: %d", config_int.INV_FF);
    LOG_INF("INV_LOWG: %d", config_int.INV_LOWG);
    LOG_INF("INV_STEP_DET: %d", config_int.INV_STEP_DET);
    LOG_INF("INV_STEP_CNT_OVFL: %d", config_int.INV_STEP_CNT_OVFL);
    LOG_INF("INV_TILT_DET: %d", config_int.INV_TILT_DET);

    // check interrupt 2
    status |= inv_imu_get_config_int2(&icm_driver, &config_int);
    LOG_INF("\nInterrupt 2.");
    LOG_INF("INV_UI_FSYNC: %d", config_int.INV_UI_FSYNC);
    LOG_INF("INV_UI_DRDY: %d", config_int.INV_UI_DRDY);
    LOG_INF("INV_FIFO_THS: %d", config_int.INV_FIFO_THS);
    LOG_INF("INV_FIFO_FULL: %d", config_int.INV_FIFO_FULL);
    LOG_INF("INV_SMD: %d", config_int.INV_SMD);
    LOG_INF("INV_WOM_X: %d", config_int.INV_WOM_X);
    LOG_INF("INV_WOM_Y: %d", config_int.INV_WOM_Y);
    LOG_INF("INV_WOM_Z: %d", config_int.INV_WOM_Z);
    LOG_INF("INV_FF: %d", config_int.INV_FF);
    LOG_INF("INV_LOWG: %d", config_int.INV_LOWG);
    LOG_INF("INV_STEP_DET: %d", config_int.INV_STEP_DET);
    LOG_INF("INV_STEP_CNT_OVFL: %d", config_int.INV_STEP_CNT_OVFL);
    LOG_INF("INV_TILT_DET: %d", config_int.INV_TILT_DET);

    // check the actual interrupt status register
    LOG_INF("Printing out some critical IMU interrupt registers ...");
    uint8_t reg_data = readIMUReg(INT_STATUS);
    LOG_INF("\n\tINT_STATUS = [0x%x]", reg_data);
    reg_data = readIMUReg(INT_CONFIG);
    LOG_INF("\tINT_CONFIG[0x%x] = 0x%x", INT_CONFIG, reg_data);
    reg_data = readIMUReg(INT_CONFIG0_MREG1);
    LOG_INF("\tINT_CONFIG0[0x%x] = 0x%x", INT_CONFIG0_MREG1, reg_data);
    reg_data = readIMUReg(INT_CONFIG1_MREG1);
    LOG_INF("\tINT_CONFIG1[0x%x] = 0x%x", INT_CONFIG1_MREG1, reg_data);
}



/*
 * init_icm: initializes the IMU. Sets some needed serial interface parameters and calls the driver init function
 */
int init_icm() {
    LOG_INF("\nInitialization IMU.");
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
        LOG_INF("Error with IMU initialization.");
        return rc;
    }

    /* Check WHOAMI */
    rc = inv_imu_get_who_am_i(&icm_driver, &who_am_i);
    if(rc != 0) {
        return -2;
    }

    if (who_am_i != INV_IMU_WHOAMI) {
        return -3;
    }

    LOG_INF("\tgot IMU WHOAMI: [0x%x]", who_am_i);    
    LOG_INF("\nDone initializing IMU");
    return 0;
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

    // configure FIFO
    rc |= inv_imu_configure_fifo(&icm_driver, INV_IMU_FIFO_ENABLED);
    rc |= inv_imu_write_reg(&icm_driver, FIFO_CONFIG2, 1, &fifo_watermark);

    // Set fifo_wm_int_w generating condition : fifo_wm_int_w generated when counter == threshold
    rc |= inv_imu_read_reg(&icm_driver, FIFO_CONFIG5_MREG1, 1, &data);
    data &= (uint8_t)~FIFO_CONFIG5_WM_GT_TH_EN;
    rc |= inv_imu_write_reg(&icm_driver, FIFO_CONFIG5_MREG1, 1, &data);
    
    // Disable APEX to use 2.25kB of fifo for raw data
    data = SENSOR_CONFIG3_APEX_DISABLE_MASK;
    rc |= inv_imu_write_reg(&icm_driver, SENSOR_CONFIG3_MREG1, 1, &data);

    // do some Ders verification
    LOG_INF("Printing out some critical IMU FIFO registers ...");
    int reg_data = readIMUReg(INTF_CONFIG0);
    LOG_INF("\tINTF_CONFIG0[0x%x] = 0x%x", INTF_CONFIG0, reg_data);
    reg_data = readIMUReg(FIFO_CONFIG1);
    LOG_INF("\tFIFO_CONFIG1[0x%x] = 0x%x", FIFO_CONFIG1, reg_data);
    reg_data = readIMUReg(FIFO_CONFIG2);
    LOG_INF("\tFIFO_CONFIG2[0x%x] = 0x%x", FIFO_CONFIG2, reg_data);
    reg_data = readIMUReg(FIFO_CONFIG3);
    LOG_INF("\tFIFO_CONFIG3[0x%x] = 0x%x", FIFO_CONFIG3, reg_data);

    // read some registers from the MREG1
    reg_data = readIMUReg(TMST_CONFIG1_MREG1);
    LOG_INF("\tTMST_CONFIG1[0x%x] = 0x%x", TMST_CONFIG1_MREG1, reg_data);
    reg_data = readIMUReg(FIFO_CONFIG5_MREG1);
    LOG_INF("\tFIFO_CONFIG5[0x%x] = 0x%x", FIFO_CONFIG5_MREG1, reg_data);
    reg_data = readIMUReg(FIFO_CONFIG6_MREG1);
    LOG_INF("\tFIFO_CONFIG6[0x%x] = 0x%x", FIFO_CONFIG6_MREG1, reg_data);
    reg_data = readIMUReg(SENSOR_CONFIG3_MREG1);
    LOG_INF("\tSENSOR_CONFIG3_MREG1[0x%x] = 0x%x", SENSOR_CONFIG3_MREG1, reg_data);

    // print out final interrupt configuration
    /* checkInterruptIMU(display); */

   return rc;
}


int startApex() {
    int rc = 0;
    inv_imu_apex_parameters_t apex_inputs;

    /* Disabling FIFO usage to optimize power consumption */
    rc |= inv_imu_configure_fifo(&icm_driver, INV_IMU_FIFO_DISABLED);

    /* Enable accel in LP mode */
    rc |= inv_imu_enable_accel_low_power_mode(&icm_driver);

    /* Disable APEX like features before enabling them */
    rc |= inv_imu_apex_disable_pedometer(&icm_driver);
    rc |= inv_imu_apex_disable_tilt(&icm_driver);
    rc |= inv_imu_disable_wom(&icm_driver);

    rc |= inv_imu_set_accel_frequency(&icm_driver, ACCEL_CONFIG0_ODR_50_HZ);
    rc |= inv_imu_apex_set_frequency(&icm_driver, APEX_CONFIG1_DMP_ODR_50Hz);

    /* Set APEX parameters */
    rc |= inv_imu_apex_init_parameters_struct(&icm_driver, &apex_inputs);
    apex_inputs.power_save =APEX_CONFIG0_DMP_POWER_SAVE_DIS;
    rc |= inv_imu_apex_configure_parameters(&icm_driver, &apex_inputs);

    // ENABLE CERTAIN APEX FEATURES
    rc |= inv_imu_apex_enable_tilt(&icm_driver);
    rc |= inv_imu_apex_enable_pedometer(&icm_driver);
    /* rc |= inv_imu_configure_wom(&icm_driver, 20, 20, 20, 1, 1); */
    rc |= inv_imu_enable_wom(&icm_driver);

    // do some Ders verification
    /* LOG_INF("Printing out some critical IMU APEX registers ..."); */
    /* int reg_data = readIMUReg(APEX_CONFIG0); */
    /* LOG_INF("\tAPEX_CONFIG0[0x%x] = 0x%x", APEX_CONFIG0, reg_data); */
    /* reg_data = readIMUReg(APEX_CONFIG1); */
    /* LOG_INF("\tAPEX_CONFIG1[0x%x] = 0x%x", APEX_CONFIG1, reg_data); */
    /* reg_data = readIMUReg(SENSOR_CONFIG3_MREG1); */
    /* LOG_INF("\tSENSOR_CONFIG3_MREG1[0x%x] = 0x%x - should be 0x00 for APEX", SENSOR_CONFIG3_MREG1, reg_data); */
    /* reg_data = readIMUReg(WOM_CONFIG); */
    /* LOG_INF("\tWOM_CONFIG[0x%x] = 0x%x", WOM_CONFIG, reg_data); */

    // print out final interrupt configuration
    /* checkInterruptIMU(display); */

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
 *
 */
int getDataFromFifo(inv_imu_sensor_event_t *evt) {
    if (evt != NULL){
        int rc = 0;

        event = evt;
        rc |= inv_imu_get_data_from_fifo(&icm_driver);
        return rc;
    }
    else {
        return -1;
    }
}


int updateApex(void) {
    int rc = 0;
    uint8_t data;
    rc = inv_imu_read_reg(&icm_driver, INT_STATUS3, 1, &data );
    if (rc == 0)
    {
    /* Update interrupt status */
    int_status3 |= data;
    }
    return rc;
}


int getPedometer(uint32_t * step_count, float step_cadence, const char* activity) {
    int rc = 0;

    /* Read APEX interrupt status */
    rc |= updateApex();

    if (int_status3 & INT_STATUS3_STEP_CNT_OVF_INT_MASK) {
        step_cnt_ovflw++;
        /* Reset pedometer overflow internal status */
        int_status3 &= ~INT_STATUS3_STEP_CNT_OVF_INT_MASK;
    }


    if (int_status3 & (INT_STATUS3_STEP_DET_INT_MASK)) {
        inv_imu_apex_step_activity_t apex_data0;
        float nb_samples           = 0;

        /* Reset pedometer internal status */
        int_status3 &= ~INT_STATUS3_STEP_DET_INT_MASK;

        rc |= inv_imu_apex_get_data_activity(&icm_driver, &apex_data0);
        // to do: detect step counter overflow?
        *step_count = apex_data0.step_cnt + step_cnt_ovflw*(uint32_t)UINT16_MAX;
        /* Converting u6.2 to float */
        nb_samples = (apex_data0.step_cadence >> 2) +
            (float)(apex_data0.step_cadence & 0x03) * 0.25f;
        if(nb_samples != 0)
            {
                step_cadence = (float)50 / nb_samples;
            } else {
            step_cadence = 0;
        }
//            activity = APEX_ACTIVITY[apex_data0.activity_class];
        activity = NULL;
    } else {
        return -11;
    }

    return rc;
}



// TODO: debug this function to determine valid accelerometer data
bool isAccelDataValid(inv_imu_sensor_event_t *evt) {
    return 1;
//  return (evt->sensor_mask & (1<<INV_SENSOR_ACCEL));
}


// TODO: debug this function to determine valid gyroscopic data
bool isGyroDataValid(inv_imu_sensor_event_t *evt) {
    return 1;
//  return (evt->sensor_mask & (1<<INV_SENSOR_GYRO));
}








