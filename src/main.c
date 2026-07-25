//*****************************************************************************
//!
//! @file main.c
//! @author Anders Bandt
//! @brief Main code for WWD with Nordic
//! @version 0.9
//! @date December 2025
//!
//! BRING-UP MODE: everything is stripped out except a 1 Hz toggle of DISP_DC
//! (P0.29), explicit USB CDC ACM bring-up, and a 1 Hz heartbeat string written
//! to the CDC port so host-side serial logging can be exercised.
//! Logs go over SEGGER RTT, not the USB console — the USB port is the thing
//! under test. The full application main() is in git history (commit 0f89f1e).
//!
//*****************************************************************************

/* standard C */
#include <stdarg.h>
#include <stdio.h>

/* Zephyr files */
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/rtc.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/logging/log.h>
#include <zephyr/usb/usb_device.h>

LOG_MODULE_REGISTER(main, CONFIG_LOG_DEFAULT_LEVEL);

/* DISP_DC — P0.29, dc-gpios on the st7735s node */
static const struct gpio_dt_spec disp_dc =
    GPIO_DT_SPEC_GET(DT_NODELABEL(st7735s), dc_gpios);

static const struct device *const cdc_dev =
    DEVICE_DT_GET(DT_NODELABEL(cdc_acm_uart0));

static const struct device *const i2c_dev =
    DEVICE_DT_GET(DT_NODELABEL(i2c0));

static const struct device *const rtc_dev =
    DEVICE_DT_GET(DT_NODELABEL(rv3028));

#define RTC_SETTLE_TIMEOUT_MS  2000
#define RV3028_REG_STATUS      0x0E
#define RV3028_STATUS_EEBUSY   BIT(7)

/* Block until the RV-3028 is genuinely ready to be bound, or the timeout
 * expires. Two conditions, in order: the part ACKs its address at all, and its
 * POR EEPROM->RAM refresh has finished (STATUS.EEBUSY clear).
 *
 * The second one is why the automatic POST_KERNEL bind fails on a cold boot:
 * rv3028_init() -> rv3028_enter_eerd() allows EEBUSY only 100 ms before
 * returning -ETIME, and init reports any error as -ENODEV. Zephyr then latches
 * that failure for the whole boot (do_device_init() sets initialized=true even
 * on error, so device_init() afterwards returns -EALREADY).
 *
 * Returns 0 when ready, -ETIMEDOUT otherwise. */
static int rtc_wait_ready(void)
{
    int64_t t0 = k_uptime_get();
    uint8_t status;
    uint8_t dummy;

    while ((k_uptime_get() - t0) < RTC_SETTLE_TIMEOUT_MS) {
        if (i2c_read(i2c_dev, &dummy, 1, 0x52) == 0 &&
            i2c_reg_read_byte(i2c_dev, 0x52, RV3028_REG_STATUS, &status) == 0 &&
            !(status & RV3028_STATUS_EEBUSY)) {
            return 0;
        }
        k_msleep(1);
    }

    return -ETIMEDOUT;
}

/* RV-3028-C7 register map (subset) */
#define RV3028_REG_SECONDS 0x00
#define RV3028_REG_ID      0x28

/* Write straight to the CDC endpoint. printk() cannot be used here: with
 * CONFIG_LOG_PRINTK=y it is routed into the log subsystem, which during
 * bring-up only has the RTT backend, so it never reaches USB. */
static void cdc_write(const char *s)
{
    while (*s != '\0') {
        uart_poll_out(cdc_dev, *s++);
    }
}

static void cdc_printf(const char *fmt, ...)
{
    char buf[160];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    cdc_write(buf);
}

/* Probe every address on i2c0. NOTE: the RV-3028 at 0x52 is currently the
 * only IC populated on this bus — the mcp23008@20 node in the DTS is not
 * fitted on any board, so its absence from the scan is expected and there is
 * no second device to use as a bus-health control. */
static void i2c_bus_scan(void)
{
    uint8_t dummy;
    int found = 0;

    cdc_write("I2C scan on i2c0:\r\n");

    for (uint8_t addr = 0x08; addr <= 0x77; addr++) {
        /* 1-byte read rather than a zero-length write — nRF TWIM does not
         * handle zero-length transfers reliably. */
        if (i2c_read(i2c_dev, &dummy, 1, addr) == 0) {
            cdc_printf("  ACK 0x%02x%s\r\n", addr,
                       addr == 0x20 ? "  (MCP23008)" :
                       addr == 0x52 ? "  (RV-3028)"  : "");
            found++;
        }
    }

    if (found == 0) {
        cdc_write("  no device ACKed (0x52 expected)\r\n");
    }
}

static void rv3028_probe(void)
{
    uint8_t regs[7];
    uint8_t id;
    int ret;

    /* Zephyr driver binding — fails if the chip did not respond at init */
    cdc_printf("rv3028 device_is_ready: %s\r\n",
               device_is_ready(rtc_dev) ? "YES" : "NO");

    /* BRING-UP DIAG: the driver's init errno, so a bind failure is not silent.
     * init_res holds -errno from rv3028_init() (19 = ENODEV). */
    cdc_printf("  state: initialized=%d init_res=%d\r\n",
               rtc_dev->state->initialized, rtc_dev->state->init_res);

    /* The node is marked zephyr,deferred-init, so bind it here — but only once
     * the part is actually ready, never relying on incidental boot delay. This
     * is a one-shot: a failed init is latched by the kernel and cannot be
     * retried, so it is worth waiting for. */
    if (!device_is_ready(rtc_dev)) {
        int w = rtc_wait_ready();
        int r = device_init(rtc_dev);

        cdc_printf("  wait_ready: %d, device_init: %d -> ready: %s\r\n",
                   w, r, device_is_ready(rtc_dev) ? "YES" : "NO");
    }

    ret = i2c_reg_read_byte(i2c_dev, 0x52, RV3028_REG_ID, &id);
    if (ret != 0) {
        cdc_printf("ID read FAILED (%d) — no I2C comms with RV-3028\r\n", ret);
        return;
    }
    cdc_printf("ID reg 0x28 = 0x%02x\r\n", id);

    ret = i2c_burst_read(i2c_dev, 0x52, RV3028_REG_SECONDS, regs, sizeof(regs));
    if (ret != 0) {
        cdc_printf("time regs read FAILED (%d)\r\n", ret);
        return;
    }
    cdc_printf("regs 0x00-0x06 (BCD): %02x %02x %02x %02x %02x %02x %02x\r\n",
               regs[0], regs[1], regs[2], regs[3], regs[4], regs[5], regs[6]);
}

/* Read the seconds register (BCD -> binary). Returns -1 on I2C error. */
static int rv3028_seconds(void)
{
    uint8_t s;

    if (i2c_reg_read_byte(i2c_dev, 0x52, RV3028_REG_SECONDS, &s) != 0) {
        return -1;
    }
    return ((s >> 4) & 0x07) * 10 + (s & 0x0f);
}

static const char *usb_status_str(enum usb_dc_status_code status)
{
    switch (status) {
    case USB_DC_ERROR:        return "ERROR";
    case USB_DC_RESET:        return "RESET";
    case USB_DC_CONNECTED:    return "CONNECTED";
    case USB_DC_CONFIGURED:   return "CONFIGURED";
    case USB_DC_DISCONNECTED: return "DISCONNECTED";
    case USB_DC_SUSPEND:      return "SUSPEND";
    case USB_DC_RESUME:       return "RESUME";
    case USB_DC_INTERFACE:    return "INTERFACE";
    case USB_DC_SET_HALT:     return "SET_HALT";
    case USB_DC_CLEAR_HALT:   return "CLEAR_HALT";
    case USB_DC_SOF:          return "SOF";
    case USB_DC_UNKNOWN:      return "UNKNOWN";
    default:                  return "???";
    }
}

static void usb_status_cb(enum usb_dc_status_code status, const uint8_t *param)
{
    /* SOF fires every 1 ms — far too noisy to log. */
    if (status == USB_DC_SOF) {
        return;
    }
    LOG_INF("USB status: %s (%d)", usb_status_str(status), status);
}

int main(void)
{
    int ret;

    LOG_INF("=== WWD-n bring-up: DISP_DC blink + USB CDC ===");

    if (!device_is_ready(cdc_dev)) {
        LOG_ERR("cdc_acm_uart0 device not ready");
    } else {
        LOG_INF("cdc_acm_uart0 ready");
    }

    ret = usb_enable(usb_status_cb);
    if (ret != 0) {
        LOG_ERR("usb_enable() failed: %d", ret);
    } else {
        LOG_INF("usb_enable() ok");
    }

    if (!gpio_is_ready_dt(&disp_dc)) {
        LOG_ERR("DISP_DC gpio not ready");
        return -1;
    }
    gpio_pin_configure_dt(&disp_dc, GPIO_OUTPUT_INACTIVE);

    bool dc_level = false;

    /* Wait for a host terminal to attach (DTR) before the one-shot probe
     * output, otherwise it scrolls past before anyone is listening. A fixed
     * k_msleep() here used to race the host: connects landing just after it
     * lost the whole probe block. Bounded so an unattended boot still runs.
     *
     * NB this also delays the deferred device_init() below, so it hands the
     * RV-3028 a settle margin a shipped build would not have — see the
     * "settle:" measurement for the number that actually matters. */
    {
        uint32_t dtr = 0;
        int64_t t_dtr = k_uptime_get();

        while (!dtr && (k_uptime_get() - t_dtr) < 3000) {
            uart_line_ctrl_get(cdc_dev, UART_LINE_CTRL_DTR, &dtr);
            k_msleep(50);
        }
        /* DTR asserts the instant the host opens the port, but the reader on
         * the far side may not be consuming yet — 100 ms lost the first lines
         * of the block. Give it a full second. */
        k_msleep(1000);
    }

    cdc_write("\r\n===== I2C / RV-3028-C7 probe =====\r\n");
    if (!device_is_ready(i2c_dev)) {
        cdc_write("i2c0 NOT ready — bus driver failed to init\r\n");
    } else {
        i2c_bus_scan();
        rv3028_probe();
    }
    cdc_write("==================================\r\n");

    while (1) {
        int secs = rv3028_seconds();

        dc_level = !dc_level;
        gpio_pin_set_dt(&disp_dc, dc_level);

        if (secs < 0) {
            cdc_write("... heartbeat ...  rtc: I2C ERR\r\n");
        } else {
            cdc_printf("... heartbeat ...  rtc secs: %02d\r\n", secs);
        }
        k_msleep(1000);
    }

    return 0;
}
