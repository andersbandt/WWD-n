//*****************************************************************************
//!
//! @file protocol.h
//! @author Anders Bandt
//! @brief Binary host<->device command protocol over the dedicated
//!        cdc_acm_uart1 USB CDC ACM interface (see nrf52833_ders.dts).
//!        cdc_acm_uart0 remains the human-readable console/shell/log stream;
//!        this channel never carries log text, so framing does not need to
//!        coexist with interleaved ASCII output.
//! @version 0.1
//! @date 2026-07-26
//!
//*****************************************************************************

#ifndef SRC_COMM_PROTOCOL_H_
#define SRC_COMM_PROTOCOL_H_

#include <stdint.h>

// Frame format on the wire, both directions:
//
//   [0]        STX      0xAA (resync marker)
//   [1]        CMD      1 byte, see enum protocol_cmd
//   [2:3]      LEN      uint16, big-endian, payload length
//   [4:4+LEN)  PAYLOAD  LEN bytes
//   [+4]       CRC32    big-endian, standard (zlib-compatible) CRC32 over
//                        CMD + LEN(big-endian) + PAYLOAD
//
// Max payload is PROTOCOL_MAX_PAYLOAD; larger frames are rejected and the
// parser resyncs on the next STX.

#define PROTOCOL_STX          0xAA
// Must comfortably fit one full NAND page (2176 B on the MT29F2G01) plus the
// 4-byte address prefix used by CMD_DUMP_DATA — mt29f_read() only accepts
// lengths that are exact multiples of the page size, so dump chunks can't be
// smaller than a page.
#define PROTOCOL_MAX_PAYLOAD  2200

enum protocol_cmd {
    CMD_PING       = 0x01, // host->device, empty payload. Device replies CMD_ACK.
    CMD_DUMP_START = 0x02, // host->device, empty payload. Streams the whole
                            // committed log (offset 0..write_addr) back as a
                            // CMD_ACK (payload = echoed cmd(1) + total_bytes(4BE),
                            // so the host can show progress as a percentage),
                            // then a run of CMD_DUMP_DATA frames, then a
                            // closing CMD_DUMP_DONE.
    CMD_DUMP_DATA  = 0x03, // device->host only. payload = addr(4BE) + raw flash bytes.
    CMD_DUMP_DONE  = 0x04, // device->host only. payload = total_bytes(4BE) + crc32(4BE),
                            // crc32 is the standard CRC32 of the concatenated
                            // flash bytes (not including the per-frame addr fields).
    CMD_ERASE      = 0x05, // host->device, empty payload. Erases the whole
                            // chip (blocking, several seconds) and resets
                            // write_addr/metadata_seq to 0, then replies
                            // CMD_ACK. Irreversible — the host should confirm
                            // with the user before sending this.
    CMD_SET_RATE   = 0x06, // host->device. payload = imu_odr_hz(2BE) +
                            // temp_interval_sec(2BE). Applies immediately
                            // (see src/comm/rate_config.h for valid ranges)
                            // and replies CMD_ACK with the same payload
                            // shape as CMD_GET_RATE (the values actually in
                            // effect after validation), or CMD_ERR(ERR_BAD_RATE)
                            // if either field was out of range — in which
                            // case NEITHER value was changed.
    CMD_GET_RATE   = 0x07, // host->device, empty payload. Device replies
                            // CMD_ACK, payload = imu_odr_hz(2BE) +
                            // temp_interval_sec(2BE).
    CMD_ACK        = 0x7E, // device->host. payload = [echoed cmd] (+ command-
                            // specific trailing fields, see above)
    CMD_ERR        = 0x7F, // device->host. payload = [echoed cmd][error code]
};

enum protocol_err {
    ERR_NONE            = 0x00,
    ERR_UNKNOWN_CMD     = 0x01,
    ERR_NOT_IMPLEMENTED = 0x02,
    ERR_NVS_NOT_READY   = 0x03,
    ERR_NVS_READ_FAIL   = 0x04,
    ERR_BAD_RATE        = 0x05,
};

// Brings up cdc_acm_uart1 and starts the RX/dispatch thread. Call once from
// main(), after usb_enable() has brought the USB device stack up.
void protocol_init(void);

#endif /* SRC_COMM_PROTOCOL_H_ */
