//*****************************************************************************
//!
//! @file uart.h
//! @author Anders Bandt
//! @brief UART protocol for dumping flash memory (and maybe some other things)
//! @version 0.9
//! @date January 2026
//!
//*****************************************************************************

#ifndef SRC_COMM_UART_H_
#define SRC_COMM_UART_H_

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define DUMP_MAGIC 0x44554D50  // "DUMP"

// Packet format (all multi-byte values in big-endian/network byte order):
// - magic:  4 bytes (0x44554D50)
// - addr:   4 bytes (absolute flash address)
// - len:    2 bytes (payload length, max 512)
// - data:   'len' bytes (payload)
// - crc32:  4 bytes (CRC32 over big-endian addr + len + data)

struct dump_state {
    uint32_t addr;          // current flash address
    uint32_t remaining;     // bytes left to dump
    bool active;            // dump in progress
};

void dump_uart_init(void);
void dump_tx(const void *data, size_t len);
int dump_send_chunk(struct dump_state *st);

#endif /* SRC_COMM_UART_H_ */