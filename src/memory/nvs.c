//*****************************************************************************
//!
//! @file nvs.c
//! @author Anders Bandt
//! @brief Controls the non-volatile memory (NVS) of the system
//! @version 0.9
//! @date March 2024
//!
//*****************************************************************************

/* Standard C99 stuff */
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>

/* Zephyr files */
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

/* My header files  */
#include <nvs.h>
#include <mt29f_nand.h>


LOG_MODULE_REGISTER(nvs, LOG_LEVEL_INF);


#define OFFSET_STARTUP_READ   (0)
int addr_offset = 0;
bool offset_status = 0;


static const mt29f_cfg_t cfg = {
    .num_dies = 2,
    .blocks_per_die = 1024,
    .pages_per_block = 64,
    .bytes_per_page = 2176,
    .oob_bytes = 128
};


#define TOTAL_PAGES cfg.num_dies * cfg.blocks_per_die * cfg.pages_per_block

static off_t write_addr = 0;
static off_t read_addr = 0;


/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//! -----------------------------------------------------------------------------------------------------------------------//
//! GLOBAL FUNCTIONS -------------------------------------------------------------------------------------------------------------//
//! -----------------------------------------------------------------------------------------------------------------------//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void nvs_init(void)
{
    mt29f_init(&cfg);
}


/*
 * nvs_write: performs a write on an NVS memory instance
 */
int nvs_write(void * data, size_t len) {
    int status = mt29f_write(write_addr, data, len);
    write_addr += len;
    return status;
}


/*
 * nvs_write_auto_offsets: performs a write operation with auto-increment of the "fresh space" offset
 */
int nvs_write_auto_offset(void * data, size_t len) {
    // TODO: figure out how this gets going with my new MT29F config
//     if (addr_offset > regionAttrs.regionSize) {
//         return -1;
//     }

    int status = mt29f_write(addr_offset, data, len);

    addr_offset += len; // increment the offset up by 1
    return status;
}


/*
 * nvs_read: performs a read on an NVS memory instance
 */
int nvs_read(void * buffer, size_t len, off_t addr) {
    mt29f_read(addr, buffer, len);
    return len;
}


/*
 * nvs_get_offset: calculates the NVS address offset
 */
// TODO: this function needs to be redone with the new page constraint I have
bool nvs_calc_offset() {
    // look for unsigned int (0xFF / 255)
    // if looking for int8_t will be looking for (-1)
    uint8_t read_data[cfg.bytes_per_page];


    // sweep whole region size
    for (int page = 0; page < TOTAL_PAGES; page++) {
        nvs_read(read_data, page, cfg.bytes_per_page); // data, offset, num bytes

        // walk records inside page
                /* Walk records inside page */
        for (uint32_t r = 0; r < cfg.bytes_per_page; r++) {
            uint8_t *rec = &read_data[r];

            if (rec[0] == 0xFF &&
                rec[1] == 0xFF &&
                rec[2] == 0xFF) {

                addr_offset = (page * cfg.bytes_per_page) + r;
                return true;
            }
        }
    }
    return false;
}


/*
 * nvs_get_addr_offset: "Getter" function for the address offset
 */
int nvs_get_addr_offset() {
    return addr_offset;
}


/*
 * nvs_get_region_size: gets the region size of the NVS
 */
size_t nvs_get_region_size() {
    // NVS_getAttrs(nvsHandle, &regionAttrs);
    // return regionAttrs.regionSize;
}





/*
 * nvs_erase_region: erases the whole NVS region
 */
void nvs_erase_region() {
    LOG_INFO("Erasing flash REGION... like the whole thing...\n");

    mt29f_chip_erase();

    // regenerate the address offset
    nvs_calc_offset();
    LOG_INFO("\tnew write offset set at: [%d]", addr_offset);
}



/*
 * nvs_close: closes an NVS instance
 */
void nvs_close() {
    // NVS_close(nvsHandle);
}


/*
 * nvs_error: takes in an NVS error status (mostly for write operation) and prints out over Display the error code
 */
void nvs_error(int error_status, bool report_success) {
    // // return if display is NULL because fucked otherwise. Function won't do nothing.
    // if (display == NULL) {
    //     return;
    // }
    // switch (error_status)
    // {
    //     case NVS_STATUS_SUCCESS:
    //         if (report_success) {
    //             LOG_INFO("NVS status success.");
    //             return;
    //         }
    //         break;
    //     case NVS_STATUS_ERROR:
    //         LOG_INFO("If the internal flash write operation failed, or if #NVS_WRITE_POST_VERIFY was requested and the destination flash range does not match the source");
    //         break;
    //     case NVS_STATUS_INV_OFFSET:
    //         LOG_INFO("If @p offset + @p size exceed the size of the region.");
    //         break;
    //     case NVS_STATUS_INV_WRITE:
    //         LOG_INFO("If #NVS_WRITE_PRE_VERIFY is requested and the destination flash address range cannot be change to the values desired.");
    //         break;
    //     case NVS_STATUS_INV_ALIGNMENT:
    //         LOG_INFO("If #NVS_WRITE_ERASE is requested and @p offset is not aligned on a sector boundary");
    //         break;
    //     case NVS_STATUS_VERIFYBUFFER:
    //         LOG_INFO("If #NVS_WRITE_PRE_VERIFY or #NVS_WRITE_POST_VERIFY is requested but the verification buffer has not been configured.");
    //         break;
    //     default:
    //         LOG_INFO("NVS undefined error case!");
    //         break;
    // }
    // LOG_INFO("Error recorded at address offset: [0x%x]", addr_offset);
}



