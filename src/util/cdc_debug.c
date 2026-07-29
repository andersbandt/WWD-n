/* standard C */
#include <stdarg.h>
#include <stdio.h>

/* Zephyr files */
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/uart.h>

#include <util/cdc_debug.h>

static const struct device *const cdc_dev =
    DEVICE_DT_GET(DT_NODELABEL(cdc_acm_uart0));

/* Write straight to the CDC endpoint. printk() cannot be used here: with
 * CONFIG_LOG_PRINTK=y it is routed into the log subsystem, which during
 * bring-up only has the RTT backend, so it never reaches USB. */
void cdc_write(const char *s)
{
    while (*s != '\0') {
        uart_poll_out(cdc_dev, *s++);
    }
    /* The probe blocks below emit many lines back-to-back and were being
     * truncated mid-string — uart_poll_out drops bytes once the CDC ACM ring
     * buffer fills, since nothing throttles it. A short pause per line lets the
     * host drain. Costs nothing here and keeps the diagnostics trustworthy. */
    k_msleep(5);
}

void cdc_printf(const char *fmt, ...)
{
    char buf[160];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    cdc_write(buf);
}
