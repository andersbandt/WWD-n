/*
 * ________________________________________________________________________________________________________
 * Copyright (c) 2015-2015 InvenSense Inc. All rights reserved.
 *
 * This software, related documentation and any modifications thereto (collectively "Software") is subject
 * to InvenSense and its licensors' intellectual property rights under U.S. and international copyright
 * and other intellectual property rights laws.
 *
 * InvenSense and its licensors retain all intellectual property and proprietary rights in and to the Software
 * and any use, reproduction, disclosure or distribution of the Software without an express license agreement
 * from InvenSense is strictly prohibited.
 *
 * EXCEPT AS OTHERWISE PROVIDED IN A LICENSE AGREEMENT BETWEEN THE PARTIES, THE SOFTWARE IS
 * PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED
 * TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.
 * EXCEPT AS OTHERWISE PROVIDED IN A LICENSE AGREEMENT BETWEEN THE PARTIES, IN NO EVENT SHALL
 * INVENSENSE BE LIABLE FOR ANY DIRECT, SPECIAL, INDIRECT, INCIDENTAL, OR CONSEQUENTIAL DAMAGES, OR ANY
 * DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT,
 * NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
 * OF THE SOFTWARE.
 * ________________________________________________________________________________________________________
 */


/* Standard C99 stuff */
#include <unistd.h>
#include <stddef.h>


/* Zephyr files */
#include <zephyr/kernel.h>
#include <zephyr/drivers/spi.h>


//#include "imu/inv_imu_extfunc.h"
#include <inv_imu_transport.h>
#include <Invn/InvError.h>
#include <inv_time.h>



#ifdef USE_DERS_IMU
    #define SPI_DEV DT_COMPAT_GET_ANY_STATUS_OKAY(tdk_icm42670p)
#else
    #define SPI_DEV DT_COMPAT_GET_ANY_STATUS_OKAY(invensense_icm42670p)
#endif


#define SPI_OP SPI_OP_MODE_MASTER | SPI_WORD_SET(8) | SPI_LINES_SINGLE
static struct spi_dt_spec spi_dev = SPI_DT_SPEC_GET(SPI_DEV, SPI_OP, 0);


#define TIMEOUT_US 1000000 /* 1 sec */



/* Function definition */
static uint8_t *get_register_cache_addr(struct inv_imu_device *s, const uint32_t reg);
static int      write_sreg(struct inv_imu_device *s, uint8_t reg, uint32_t len, const uint8_t *buf);
static int      read_sreg(struct inv_imu_device *s, uint8_t reg, uint32_t len, uint8_t *buf);
static int      write_mclk_reg(struct inv_imu_device *s, uint16_t regaddr, uint8_t wr_cnt,
                               const uint8_t *buf);

static int read_mclk_reg(struct inv_imu_device *s, uint16_t regaddr, uint8_t rd_cnt, uint8_t *buf);


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


    return spi_write_dt(&spi_dev, &tx_set);
}


/* Static SPI scratch buffers — sized for the largest possible read (max_read=2048 + 1
 * register byte). Static is safe because imu_spi_read is only ever called from the
 * single imu_thread. VLAs of up to ~1600 bytes were previously stack-allocated here,
 * which overflowed the IMU thread stack and corrupted nrfx SPI driver state. */
#define IMU_SPI_MAX_XFER 2049
static uint8_t imu_spi_tx_buf[IMU_SPI_MAX_XFER];
static uint8_t imu_spi_rx_buf[IMU_SPI_MAX_XFER];

int imu_spi_read(struct inv_imu_serif *serif,
                 uint8_t reg,
                 uint8_t *buf,
                 uint32_t len)
{
    uint8_t *tx_data = imu_spi_tx_buf;
    uint8_t *rx_data = imu_spi_rx_buf;

    tx_data[0] = reg;
    memset(&tx_data[1], 0x00, len);  // dummy bytes to clock data out

    struct spi_buf tx_buf = {
        .buf = tx_data,
        .len = len + 1,
    };

    struct spi_buf_set tx_set = {
        .buffers = &tx_buf,
        .count = 1,
    };

    struct spi_buf rx_buf = {
        .buf = rx_data,
        .len = len + 1,
    };

    struct spi_buf_set rx_set = {
        .buffers = &rx_buf,
        .count = 1,
    };

    int rc = spi_transceive_dt(&spi_dev, &tx_set, &rx_set);

    // Copy received data (skip first byte which is register echo/dummy)
    memcpy(buf, &rx_data[1], len);

    // return status code (0 for success)
    return rc;
}



/*
 * inv_imu_init_transport: initializes transport layer
 */
int inv_imu_init_transport(struct inv_imu_device *s) {
	int status = 0;
	struct inv_imu_transport *t = (struct inv_imu_transport *)s;

	if (t == NULL)
		return INV_ERROR_BAD_ARG;

	status |= read_sreg(s, (uint8_t)PWR_MGMT0, 1, &(t->register_cache.pwr_mgmt0_reg));
#if ICM_IS_GYRO_SUPPORTED
	status |= read_sreg(s, (uint8_t)GYRO_CONFIG0, 1, &(t->register_cache.gyro_config0_reg));
#endif
	status |= read_sreg(s, (uint8_t)ACCEL_CONFIG0, 1, &(t->register_cache.accel_config0_reg));

	status |= read_mclk_reg(s, (TMST_CONFIG1_MREG1 & 0xFFFF), 1, &(t->register_cache.tmst_config1_reg));

	t->need_mclk_cnt = 0;

	return status;
}



int inv_imu_read_reg(struct inv_imu_device *s, uint32_t reg, uint32_t len, uint8_t *buf)
{
	int rc = 0;

	if (s == NULL) {
		return INV_ERROR_BAD_ARG;
	}

    
// TODO: evaluate the need for caching regiseter reads
//		const uint8_t *cache_addr = get_register_cache_addr(s, reg + i);
//    if (cache_addr) { buf[i] = *cache_addr; }
    

	if (!(reg & 0x10000)) {
		for (uint32_t i = 0; i < len; i++) {
			rc |= read_mclk_reg(s, ((reg + i) & 0xFFFF), 1, &buf[i]);
		}   
	}
	else {
		// NOTE: I edited the below line. Check commit history if issues or concerns arise
		rc |= read_sreg(s, (uint8_t)(reg) | 0x80, len, buf);
	}
    
	return rc;
}



int inv_imu_write_reg(struct inv_imu_device *s, uint32_t reg, uint32_t len, const uint8_t *buf)
{
	int rc = 0;

	if (s == NULL)
		return INV_ERROR_BAD_ARG;

	for (uint32_t i = 0; i < len; i++) {
		uint8_t *cache_addr = get_register_cache_addr(s, reg + i);

		if (cache_addr)
			*cache_addr = buf[i];

		if (!(reg & 0x10000))
			rc |= write_mclk_reg(s, ((reg + i) & 0xFFFF), 1, &buf[i]);
	}

	if (reg & 0x10000)
		rc |= write_sreg(s, (uint8_t)reg, len, buf);

	return rc;
}


int inv_imu_switch_on_mclk(struct inv_imu_device *s)
{
    int                       status = 0;
    uint8_t                   data;
    struct inv_imu_transport *t = (struct inv_imu_transport *)s;

    if (t == NULL)
        return INV_ERROR_BAD_ARG;

    /* set IDLE bit only if it is not set yet */
    if (t->need_mclk_cnt == 0) {
        uint64_t start;

        status |= inv_imu_read_reg(s, PWR_MGMT0, 1, &data);
        data |= PWR_MGMT0_IDLE_MASK;
        status |= inv_imu_write_reg(s, PWR_MGMT0, 1, &data);

        if (status)
            return status;

        /* Check if MCLK is ready */
        start = inv_imu_get_time_us();
        do {
            status = inv_imu_read_reg(s, MCLK_RDY, 1, &data);

            if (status)
                return status;

            /* Timeout */
            if (inv_imu_get_time_us() - start > TIMEOUT_US) {
              return INV_ERROR_TIMEOUT;
            }
        } while (!(data & MCLK_RDY_MCLK_RDY_MASK));
    } else {
        /* Make sure it is already on */
        status |= inv_imu_read_reg(s, PWR_MGMT0, 1, &data);
        if (0 == (data &= PWR_MGMT0_IDLE_MASK))
            status |= INV_ERROR;
    }

    /* Increment the counter to keep track of number of MCLK requesters */
    t->need_mclk_cnt++;

    return status;
}


int inv_imu_switch_off_mclk(struct inv_imu_device *s)
{
	int                       status = 0;
	uint8_t                   data;
	struct inv_imu_transport *t = (struct inv_imu_transport *)s;

	if (t == NULL)
		return INV_ERROR_BAD_ARG;

	/* Reset the IDLE but only if there is one requester left */
	if (t->need_mclk_cnt == 1) {
		status |= inv_imu_read_reg(s, PWR_MGMT0, 1, &data);
		data &= ~PWR_MGMT0_IDLE_MASK;
		status |= inv_imu_write_reg(s, PWR_MGMT0, 1, &data);
	} else {
		/* Make sure it is still on */
		status |= inv_imu_read_reg(s, PWR_MGMT0, 1, &data);
		if (0 == (data &= PWR_MGMT0_IDLE_MASK))
			status |= INV_ERROR;
	}

	/* Decrement the counter */
	t->need_mclk_cnt--;

	return status;
}


static uint8_t *get_register_cache_addr(struct inv_imu_device *s, const uint32_t reg)
{
	struct inv_imu_transport *t = (struct inv_imu_transport *)s;

	if (t == NULL)
		return (uint8_t *)0; /* error */

	switch (reg) {
	case PWR_MGMT0:
		return &(t->register_cache.pwr_mgmt0_reg);
#if ICM_IS_GYRO_SUPPORTED
	case GYRO_CONFIG0:
		return &(t->register_cache.gyro_config0_reg);
#endif
	case ACCEL_CONFIG0:
		return &(t->register_cache.accel_config0_reg);
	case TMST_CONFIG1_MREG1:
		return &(t->register_cache.tmst_config1_reg);
	default:
		return (uint8_t *)0; // Not found
	}
}


static int read_sreg(struct inv_imu_device *s, uint8_t reg, uint32_t len, uint8_t *buf)
{
	struct inv_imu_serif *serif = (struct inv_imu_serif *)s;

	if (serif == NULL)
		return INV_ERROR_BAD_ARG;

	if (len > serif->max_read)
		return INV_ERROR_SIZE;

	if (serif->read_reg(serif, reg, buf, len) != 0)
		return INV_ERROR_TRANSPORT;

	return 0;
}


static int write_sreg(struct inv_imu_device *s, uint8_t reg, uint32_t len, const uint8_t *buf)
{
	struct inv_imu_serif *serif = (struct inv_imu_serif *)s;

	if (serif == NULL)
		return INV_ERROR_BAD_ARG;

	if (len > serif->max_write)
		return INV_ERROR_SIZE;

	if (serif->write_reg(serif, reg, buf, len) != 0)
		return INV_ERROR_TRANSPORT;

	return 0;
}


static int read_mclk_reg(struct inv_imu_device *s, uint16_t regaddr, uint8_t rd_cnt, uint8_t *buf)
{
	uint8_t data;
	uint8_t blk_sel = (regaddr & 0xFF00) >> 8;
	int     status  = 0;

	if (s == NULL)
		return INV_ERROR_BAD_ARG;

	// Have IMU not in IDLE mode to access MCLK domain
	status |= inv_imu_switch_on_mclk(s);
    k_usleep(10); // added by Anders

	// optimize by changing BLK_SEL only if not NULL
	if (blk_sel)
        status |= write_sreg(s, (uint8_t)BLK_SEL_R & 0xff, 1, &blk_sel);

	data = (regaddr & 0x00FF);
	status |= write_sreg(s, (uint8_t)MADDR_R, 1, &data);
	k_usleep(10);
    status |= read_sreg(s, (uint8_t)M_R | 0x80, rd_cnt, buf); // OR operation with 0x80 masks bit 7 to 1 for R operation
    k_usleep(10);
    
	if (blk_sel) {
		data = 0;
		status |= write_sreg(s, (uint8_t)BLK_SEL_R, 1, &data);
	}


	// switch OFF MCLK if needed
	status |= inv_imu_switch_off_mclk(s);

	return status;
}


static int write_mclk_reg(struct inv_imu_device *s, uint16_t regaddr, uint8_t wr_cnt,
                          const uint8_t *buf)
{
	uint8_t data;
	uint8_t blk_sel = (regaddr & 0xFF00) >> 8;
	int     status  = 0;

	if (s == NULL)
		return INV_ERROR_BAD_ARG;

	// Have IMU not in IDLE mode to access MCLK domain
	status |= inv_imu_switch_on_mclk(s);

	// optimize by changing BLK_SEL only if not NULL
	if (blk_sel)
		status |= write_sreg(s, (uint8_t)BLK_SEL_W, 1, &blk_sel);

	data = (regaddr & 0x00FF);
	status |= write_sreg(s, (uint8_t)MADDR_W, 1, &data);
	for (uint8_t i = 0; i < wr_cnt; i++) {
		status |= write_sreg(s, (uint8_t)M_W, 1, &buf[i]);
		k_usleep(10);
	}

	if (blk_sel) {
		data   = 0;
		status = write_sreg(s, (uint8_t)BLK_SEL_W, 1, &data);
	}

	status |= inv_imu_switch_off_mclk(s);

	return status;
}
