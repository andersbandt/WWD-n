//*****************************************************************************
//!
//! @file uart.c
//! @author Anders Bandt
//! @brief UART protocol for dumping flash memory (and maybe other things)
//! @version 0.9
//! @date January 2026
//!
//*****************************************************************************

/* Zephyr files */
#include <zephyr/sys/crc.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/drivers/uart.h>

/* Project files */
#include "uart.h"
#include "../memory/nvs.h"



#define DUMP_CHUNK_SIZE 512   // start conservative


static const struct device *uart;

void dump_uart_init(void)
{
    uart = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));
}



void dump_tx(const void *data, size_t len)
{
    const uint8_t *p = data;
    for (size_t i = 0; i < len; i++) {
        uart_poll_out(uart, p[i]);
    }
}



static uint32_t dump_crc(uint32_t addr, uint16_t len, uint8_t *data)
{
    uint32_t crc = 0xFFFFFFFF;

    // CRC calculated over big-endian representations
    uint32_t addr_be = sys_cpu_to_be32(addr);
    uint16_t len_be = sys_cpu_to_be16(len);

    crc = crc32_ieee_update(crc, (uint8_t *)&addr_be, sizeof(addr_be));
    crc = crc32_ieee_update(crc, (uint8_t *)&len_be, sizeof(len_be));
    crc = crc32_ieee_update(crc, data, len);

    return crc;
}


int dump_send_chunk(struct dump_state *st)
{
    uint8_t buf[DUMP_CHUNK_SIZE];
    uint16_t len = MIN(DUMP_CHUNK_SIZE, st->remaining);
    int rc;

    // Read flash data
    rc = nvs_read(buf, len, st->addr);
    if (rc < 0) {
        return rc;  // Read failed
    }

    // Calculate CRC on native values (function handles endianness internally)
    uint32_t crc = dump_crc(st->addr, len, buf);

    // Convert to big-endian for transmission
    uint32_t magic_be = sys_cpu_to_be32(DUMP_MAGIC);
    uint32_t addr_be  = sys_cpu_to_be32(st->addr);
    uint16_t len_be   = sys_cpu_to_be16(len);
    uint32_t crc_be   = sys_cpu_to_be32(crc);

    // Transmit packet
    dump_tx(&magic_be, sizeof(magic_be));
    dump_tx(&addr_be,  sizeof(addr_be));
    dump_tx(&len_be,   sizeof(len_be));
    dump_tx(buf,       len);
    dump_tx(&crc_be,   sizeof(crc_be));

    // Update state
    st->addr      += len;
    st->remaining -= len;

    if (st->remaining == 0) {
        st->active = false;
    }

    return 0;
}

