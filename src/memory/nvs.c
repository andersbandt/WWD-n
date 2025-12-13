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



/* My header files  */
#include <nvs.h>

#define OFFSET_STARTUP_READ   (0)


// NVS_Handle nvsHandle;
// NVS_Attrs regionAttrs;
// NVS_Params nvsParams;


int addr_offset = 0;
bool offset_status = 0;


/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//! -----------------------------------------------------------------------------------------------------------------------//
//! FUNCTIONS -------------------------------------------------------------------------------------------------------------//
//! -----------------------------------------------------------------------------------------------------------------------//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


int nvs_init() {
    // NVS_init();
    // NVS_Params_init(&nvsParams);
    // nvsHandle = NVS_open(CONFIG_NVS_0, &nvsParams);

    // if (nvsHandle == NULL) {
    //     LOG_INFO("NVS_open() failed.");
    //     return -1;
    // }

    // // populate a NVS_Attrs structure with properties specific to a NVS_Handle
    // //  like region base address, region size, sector size
    // NVS_getAttrs(nvsHandle, &regionAttrs);
    // /* Display the NVS region attributes */
    // LOG_INFO("Region Base Address: 0x%x", regionAttrs.regionBase);
    // LOG_INFO("Region Size: 0x%x", regionAttrs.regionSize);
    // LOG_INFO("Sector Size: 0x%x", regionAttrs.sectorSize);
    // LOG_INFO("NUM sectors: %d\n", regionAttrs.regionSize/regionAttrs.sectorSize);

    // // try to get "fresh" address offset
    // offset_status = nvs_calc_offset(display);
    // LOG_INFO("Offset address search status: %d", offset_status);
    // LOG_INFO("\taddress set at: [%d]", addr_offset);

	// // add data start signifier
	// if (addr_offset == 0) {
	//   LOG_INFO("Fresh address space detected. Writing 3 bytes for start.");
	//   uint8_t flash_start_sig[3];
	//   flash_start_sig[0] = 0xAB;
	//   flash_start_sig[1] = 0xCD;
	//   flash_start_sig[2] = 0xEF;
	//   nvs_write_auto_offset(flash_start_sig, 3);
	// }
	
    // // perform read to end of non-writable space
    // if (OFFSET_STARTUP_READ) {
    //     LOG_INFO("\n\tPerforming reads until offset plus 10\%");
    //     uint8_t reg_read[1];
    //     for(int i = 0; i < (int)addr_offset*1.1; i++) {
    //         nvs_read(reg_read, i, 1); // read 1 byte at a time
    //         LOG_INFO("\taddr offset[%d]: [0x%x]", i, reg_read[0]);
    //     }
    // }

    // return
    return 1;
}


/*
 * nvs_close: closes an NVS instance
 */
void nvs_close() {
    // NVS_close(nvsHandle);
}


/*
 * nvs_get_offset: calculates the NVS address offset
 */
bool nvs_calc_offset() {
    // look for unsigned int (0xFF / 255)
    // if looking for int8_t will be looking for (-1)
    uint8_t read_data[3];

    // sweep whole region size
    // for (int i = 0; i < regionAttrs.regionSize; i++) {
    //     nvs_read(read_data, i*3, 3); // data, offset, num bytes
    //     if (read_data[0] == 0xFF && read_data[1] == 0xFF && read_data[2] == 0xFF) {
    //         addr_offset = i;
    //         return true;
    //     }
    // }
    return false;
}


/*
 * nvs_get_addr_offset: "Getter" function for the address offset
 */
int nvs_get_addr_offset() {
    // return addr_offset;
}


/*
 * nvs_get_region_size: gets the region size of the NVS
 */
size_t nvs_get_region_size() {
    // NVS_getAttrs(nvsHandle, &regionAttrs);
    // return regionAttrs.regionSize;
}


/*
 * nvs_write: performs a write on an NVS memory instance
 */
int nvs_write(void * data, size_t offset, size_t num_bytes) {
    // write "Hello" to the base address of region 0, verify after write
//    status = NVS_write(nvsRegion, 0, "Hello", strlen("Hello")+1, NVS_WRITE_POST_VERIFY);

//     int status = NVS_write(nvsHandle, // handle
//                        offset, // offset
//                        data, // buffer
//                        num_bytes, // number of bytes
// //                       NVS_WRITE_ERASE);
// //                       NVS_WRITE_POST_VERIFY); // write flags
//                        NVS_WRITE_PRE_VERIFY);
//     return status;
}


/*
 * nvs_write_auto_offsets: performs a write operation with auto-increment of the "fresh space" offset
 */
int nvs_write_auto_offset(void * data, size_t num_bytes) {
//     if (addr_offset > regionAttrs.regionSize) {
//         return -1;
//     }

//   int status = NVS_write(nvsHandle, // handle
//                          addr_offset, // offset - AUTOMATIC based on set `addr_offset` variable
//                          data, // buffer
//                          num_bytes, // number of bytes
//                          NVS_WRITE_POST_VERIFY); // write flags

//     addr_offset += num_bytes; // increment the offset up by 1
//     return status;
}


/*
 * nvs_read: performs a read on an NVS memory instance
 */
int nvs_read(void * buffer, size_t offset, size_t num_bytes) {
    /*
     * Copy "sizeof(signature)" bytes from the NVS region base address into
     * buffer. An offset of 0 specifies the offset from region base address.
     * Therefore, the bytes are copied from regionAttrs.regionBase.
     */
    // NVS_read(nvsHandle, offset, (void *)buffer, num_bytes);
    return 1;
}


/*
 * nvs_erase_region: erases the whole NVS region
 */
void nvs_erase_region() {
    // LOG_INFO("%s", regionAttrs.regionBase);
    // LOG_INFO("Erasing flash REGION... like the whole thing...\n");

    // // erase the entire flash sector
    // int num_sectors = regionAttrs.regionSize/regionAttrs.sectorSize;
    // for(int i=0; i < num_sectors; i++) {
    //     int status = NVS_erase(nvsHandle, i*regionAttrs.sectorSize, regionAttrs.sectorSize);
    //     nvs_error(status, 0, display);
    // }

    // // regenerate the address offset
    // nvs_calc_offset(display);
    // LOG_INFO("\tnew write offset set at: [%d]", addr_offset);
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



