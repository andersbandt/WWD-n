#ifndef UTIL_CDC_DEBUG_H
#define UTIL_CDC_DEBUG_H

/* Write straight to the console CDC ACM endpoint (cdc_acm_uart0) — bypasses
 * the Zephyr log subsystem so bring-up diagnostics reach the host even when
 * only the RTT log backend is configured. See cdc_debug.c for why printk()
 * can't be used for this. */
void cdc_write(const char *s);
void cdc_printf(const char *fmt, ...);

#endif /* UTIL_CDC_DEBUG_H */
