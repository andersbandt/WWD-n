//*****************************************************************************
//!
//! @file nvs.h
//! @author Anders Bandt
//! @brief Anders' function for interfacing with the flash memory through NVS TI driver
//! @version 0.9
//! @date March 2024
//!
//*****************************************************************************

#ifndef SRC_DATA_NVS_H_
#define SRC_DATA_NVS_H_


/* Standard C99 stuff */
#include <stdint.h>
#include <stddef.h>



/**
 * @brief initializes the NVS handle
 */
void nvs_init();



/*
 * @brief close the NVS handle
 */
void nvs_close();


/**
 * @brief calculates address offset
 *
 * @desc reads through flash memory until sequence of 0xFF is found
 */
bool nvs_calc_offset();


/**
 * @brief getter function for address offset
 */
int nvs_get_addr_offset();


/**
 * @brief getter function for NVS region size
 */
size_t nvs_get_region_size();


/**
 * @brief performs an NVS write
 */
int nvs_write(void * data, size_t len);


/**
 * @brief performs an NVS write at calculated "fresh address offset"
 */
int nvs_write_auto_offset(void * data, size_t num_bytes);


/**
 * @brief reads the NVS
 */
int nvs_read(void * buffer, size_t len, off_t addr);


/**
 * @brief erases the whole NVS region
 */
void nvs_erase_region();


/**
 * @brief error handler for NVS
 *
 * @param[in]   error_status      received error code
 * @param[in]   report_success    flag for determining if we want to report success
 * @param[in]   display           Display handle
 */
void nvs_error(int error_status, bool report_success);




#endif /* SRC_DATA_NVS_H_ */
