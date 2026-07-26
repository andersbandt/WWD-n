//*****************************************************************************
//!
//! @file protocol.c
//! @author Anders Bandt
//! @brief Binary host<->device command protocol over cdc_acm_uart1.
//! @version 0.1
//! @date 2026-07-26
//!
//*****************************************************************************

/* Zephyr files */
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/crc.h>
#include <zephyr/sys/ring_buffer.h>
#include <string.h>

/* Project files */
#include "protocol.h"
#include "rate_config.h"
#include <nvs.h>

LOG_MODULE_REGISTER(protocol, CONFIG_LOG_DEFAULT_LEVEL);

#define RX_RINGBUF_SIZE   256

#define PROTOCOL_THREAD_STACK_SIZE 2048
#define PROTOCOL_THREAD_PRIORITY   7

static const struct device *cmd_uart;

RING_BUF_DECLARE(rx_rb, RX_RINGBUF_SIZE);
static K_SEM_DEFINE(rx_sem, 0, 1);

static void uart_isr(const struct device *dev, void *user_data)
{
    ARG_UNUSED(user_data);

    if (!uart_irq_update(dev)) {
        return;
    }

    if (uart_irq_rx_ready(dev)) {
        uint8_t data[32];
        int n = uart_fifo_read(dev, data, sizeof(data));

        if (n > 0) {
            uint32_t put = ring_buf_put(&rx_rb, data, (uint32_t)n);

            if (put < (uint32_t)n) {
                LOG_WRN("protocol: rx ring buffer overrun, %u byte(s) dropped",
                        (unsigned)((uint32_t)n - put));
            }
            k_sem_give(&rx_sem);
        }
    }
}

// ---------------------------------------------------------------------------
// TX
// ---------------------------------------------------------------------------

static uint32_t frame_crc(uint8_t cmd, uint16_t len, const uint8_t *payload)
{
    uint32_t crc = 0;
    uint16_t len_be = sys_cpu_to_be16(len);

    crc = crc32_ieee_update(crc, &cmd, 1);
    crc = crc32_ieee_update(crc, (const uint8_t *)&len_be, 2);
    if (len) {
        crc = crc32_ieee_update(crc, payload, len);
    }

    return crc;
}

/* uart_fifo_fill() (cdc_acm's TX ring buffer, drained asynchronously by the
 * USB stack's own workqueue) was tried here first for throughput, but produced
 * reproducible payload corruption on multi-KB dump frames — the device's own
 * per-frame CRC (computed and sent immediately after the data, before any
 * corruption could occur) was internally consistent across repeated dumps of
 * the same static flash content, while the bytes actually received by the
 * host differed run to run. That points at the ring-buffer producer/consumer
 * path, not at frame_crc() or the data itself. uart_poll_out() one byte at a
 * time doesn't have that problem (proven correct all session, incl. every
 * PING/ACK exchange) — the only downside was starving the USB stack on a
 * large payload if never yielded, fixed below by yielding periodically. */
static void cmd_uart_tx(const void *data, size_t len)
{
    const uint8_t *p = data;

    for (size_t i = 0; i < len; i++) {
        uart_poll_out(cmd_uart, p[i]);
        if ((i & 0x3F) == 0x3F) {
            k_yield();
        }
    }
}

static void send_frame(enum protocol_cmd cmd, const uint8_t *payload, uint16_t len)
{
    uint8_t hdr[4];
    uint16_t len_be_field = sys_cpu_to_be16(len);
    uint32_t crc = frame_crc((uint8_t)cmd, len, payload);
    uint32_t crc_be = sys_cpu_to_be32(crc);

    hdr[0] = PROTOCOL_STX;
    hdr[1] = (uint8_t)cmd;
    memcpy(&hdr[2], &len_be_field, 2);

    cmd_uart_tx(hdr, sizeof(hdr));
    if (len) {
        cmd_uart_tx(payload, len);
    }
    cmd_uart_tx(&crc_be, sizeof(crc_be));
}

static void send_ack(enum protocol_cmd for_cmd)
{
    uint8_t payload = (uint8_t)for_cmd;

    send_frame(CMD_ACK, &payload, 1);
}

static void send_err(enum protocol_cmd for_cmd, enum protocol_err err)
{
    uint8_t payload[2] = { (uint8_t)for_cmd, (uint8_t)err };

    send_frame(CMD_ERR, payload, sizeof(payload));
}

// ---------------------------------------------------------------------------
// Command handlers
// ---------------------------------------------------------------------------

static void handle_dump_start(void)
{
    if (!nvs_ready()) {
        send_err(CMD_DUMP_START, ERR_NVS_NOT_READY);
        return;
    }

    // Include whatever is still sitting in the in-memory page buffer so the
    // dump isn't missing the tail of the log (see nvs_notes.md).
    nvs_flush_buffer();

    uint32_t total = (uint32_t)nvs_get_addr_offset();

    // ACK payload = [echoed cmd(1)] + [total_bytes(4BE)] so the host can show
    // progress as a percentage instead of just a running byte count.
    uint8_t ack_payload[5];
    uint32_t ack_total_be = sys_cpu_to_be32(total);
    ack_payload[0] = (uint8_t)CMD_DUMP_START;
    memcpy(&ack_payload[1], &ack_total_be, 4);
    send_frame(CMD_ACK, ack_payload, sizeof(ack_payload));

    // nvs_read()/mt29f_read() only accept lengths that are an exact multiple
    // of the NAND page size, so every read below pulls a full page even
    // though the last one is usually only partially meaningful (trimmed to
    // `total` before it's put on the wire).
    const mt29f_cfg_t *nand_cfg = mt29f_get_config();
    uint16_t page_size = nand_cfg->bytes_per_page;

    // static: 2x PROTOCOL_MAX_PAYLOAD is too large for the protocol thread's
    // stack (PROTOCOL_THREAD_STACK_SIZE); only one dump runs at a time so
    // there's no reentrancy concern.
    static uint8_t page_chunk[PROTOCOL_MAX_PAYLOAD];
    static uint8_t frame_payload[PROTOCOL_MAX_PAYLOAD];
    uint32_t dump_crc = 0;
    uint32_t addr = 0;

    while (addr < total) {
        int rc = nvs_read((off_t)addr, page_chunk, page_size);

        if (rc < 0) {
            LOG_ERR("protocol: nvs_read failed at addr %u: %d", addr, rc);
            send_err(CMD_DUMP_START, ERR_NVS_READ_FAIL);
            return;
        }

        uint16_t send_len = (uint16_t)MIN(page_size, total - addr);

        dump_crc = crc32_ieee_update(dump_crc, page_chunk, send_len);

        uint32_t addr_be = sys_cpu_to_be32(addr);
        memcpy(frame_payload, &addr_be, 4);
        memcpy(frame_payload + 4, page_chunk, send_len);
        send_frame(CMD_DUMP_DATA, frame_payload, 4 + send_len);

        addr += page_size;
    }

    uint8_t done_payload[8];
    uint32_t total_be = sys_cpu_to_be32(total);
    uint32_t crc_be = sys_cpu_to_be32(dump_crc);

    memcpy(done_payload, &total_be, 4);
    memcpy(done_payload + 4, &crc_be, 4);
    send_frame(CMD_DUMP_DONE, done_payload, sizeof(done_payload));

    LOG_INF("protocol: dump complete, %u bytes, crc32=0x%08x", total, dump_crc);
}

static void handle_erase(void)
{
    if (!nvs_ready()) {
        send_err(CMD_ERASE, ERR_NVS_NOT_READY);
        return;
    }

    LOG_INF("protocol: erasing chip (host request)...");
    nvs_erase_chip();
    LOG_INF("protocol: erase complete");

    send_ack(CMD_ERASE);
}

static void send_rate_ack(void)
{
    uint8_t payload[5];
    uint16_t odr_be = sys_cpu_to_be16(rate_config_get_imu_odr_hz());
    uint16_t temp_be = sys_cpu_to_be16(rate_config_get_temp_interval_sec());

    payload[0] = (uint8_t)CMD_ACK;  // "for" field is meaningless here since both
                                     // CMD_GET_RATE and CMD_SET_RATE share this
                                     // reply shape; host identifies it by having
                                     // sent one of those two commands.
    memcpy(&payload[1], &odr_be, 2);
    memcpy(&payload[3], &temp_be, 2);
    send_frame(CMD_ACK, payload, sizeof(payload));
}

static void handle_get_rate(void)
{
    send_rate_ack();
}

static void handle_set_rate(const uint8_t *payload, uint16_t len)
{
    if (len < 4) {
        send_err(CMD_SET_RATE, ERR_BAD_RATE);
        return;
    }

    uint16_t odr_hz = sys_get_be16(&payload[0]);
    uint16_t temp_sec = sys_get_be16(&payload[2]);

    // Validate the (side-effect-free) temp interval before touching the IMU
    // ODR (which reprograms hardware registers), so a bad temp_sec can't
    // leave the ODR changed while reporting failure.
    if (rate_config_set_temp_interval_sec(temp_sec) != 0) {
        send_err(CMD_SET_RATE, ERR_BAD_RATE);
        return;
    }
    if (rate_config_set_imu_odr_hz(odr_hz) != 0) {
        send_err(CMD_SET_RATE, ERR_BAD_RATE);
        return;
    }

    send_rate_ack();
}

static void dispatch(uint8_t cmd, const uint8_t *payload, uint16_t len)
{
    switch (cmd) {
    case CMD_PING:
        send_ack(CMD_PING);
        break;
    case CMD_DUMP_START:
        handle_dump_start();
        break;
    case CMD_ERASE:
        handle_erase();
        break;
    case CMD_GET_RATE:
        handle_get_rate();
        break;
    case CMD_SET_RATE:
        handle_set_rate(payload, len);
        break;
    default:
        LOG_WRN("protocol: unknown command 0x%02x", cmd);
        send_err((enum protocol_cmd)cmd, ERR_UNKNOWN_CMD);
        break;
    }
}

// ---------------------------------------------------------------------------
// RX parser (byte-driven state machine, runs in the protocol thread)
// ---------------------------------------------------------------------------

enum rx_state {
    WAIT_STX,
    GET_CMD,
    GET_LEN_HI,
    GET_LEN_LO,
    GET_PAYLOAD,
    GET_CRC,
};

static void rx_byte(uint8_t byte)
{
    static enum rx_state state = WAIT_STX;
    static uint8_t  frame_cmd;
    static uint16_t frame_len;
    static uint16_t frame_idx;
    static uint8_t  frame_payload[PROTOCOL_MAX_PAYLOAD];
    static uint8_t  crc_bytes[4];
    static uint8_t  crc_idx;

    switch (state) {
    case WAIT_STX:
        if (byte == PROTOCOL_STX) {
            state = GET_CMD;
        }
        break;

    case GET_CMD:
        frame_cmd = byte;
        state = GET_LEN_HI;
        break;

    case GET_LEN_HI:
        frame_len = (uint16_t)byte << 8;
        state = GET_LEN_LO;
        break;

    case GET_LEN_LO:
        frame_len |= byte;
        frame_idx = 0;
        if (frame_len > sizeof(frame_payload)) {
            LOG_WRN("protocol: frame len %u exceeds max payload, resyncing", frame_len);
            state = WAIT_STX;
        } else {
            crc_idx = 0;
            state = frame_len ? GET_PAYLOAD : GET_CRC;
        }
        break;

    case GET_PAYLOAD:
        frame_payload[frame_idx++] = byte;
        if (frame_idx == frame_len) {
            crc_idx = 0;
            state = GET_CRC;
        }
        break;

    case GET_CRC:
        crc_bytes[crc_idx++] = byte;
        if (crc_idx == 4) {
            uint32_t rx_crc = sys_get_be32(crc_bytes);
            uint32_t calc_crc = frame_crc(frame_cmd, frame_len, frame_payload);

            if (rx_crc == calc_crc) {
                dispatch(frame_cmd, frame_payload, frame_len);
            } else {
                LOG_WRN("protocol: CRC mismatch on cmd 0x%02x, dropping frame",
                        frame_cmd);
            }
            state = WAIT_STX;
        }
        break;
    }
}

static void protocol_thread_fn(void *a, void *b, void *c)
{
    ARG_UNUSED(a);
    ARG_UNUSED(b);
    ARG_UNUSED(c);

    while (1) {
        k_sem_take(&rx_sem, K_FOREVER);

        uint8_t byte;
        while (ring_buf_get(&rx_rb, &byte, 1) == 1) {
            rx_byte(byte);
        }
    }
}

K_THREAD_STACK_DEFINE(protocol_thread_stack, PROTOCOL_THREAD_STACK_SIZE);
static struct k_thread protocol_thread_data;

void protocol_init(void)
{
    cmd_uart = DEVICE_DT_GET(DT_NODELABEL(cdc_acm_uart1));

    if (!device_is_ready(cmd_uart)) {
        LOG_ERR("protocol: cdc_acm_uart1 not ready");
        return;
    }

    uart_irq_callback_set(cmd_uart, uart_isr);
    uart_irq_rx_enable(cmd_uart);

    k_thread_create(&protocol_thread_data, protocol_thread_stack,
                     K_THREAD_STACK_SIZEOF(protocol_thread_stack),
                     protocol_thread_fn, NULL, NULL, NULL,
                     PROTOCOL_THREAD_PRIORITY, 0, K_NO_WAIT);
    k_thread_name_set(&protocol_thread_data, "protocol");

    LOG_INF("protocol: cdc_acm_uart1 command channel ready");
}
