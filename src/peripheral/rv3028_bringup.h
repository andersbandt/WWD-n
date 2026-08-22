#ifndef PERIPHERAL_RV3028_BRINGUP_H
#define PERIPHERAL_RV3028_BRINGUP_H

/* Report what the SoC's LF clock actually settled on (RC/Xtal/Synth). */
void lfclk_report(void);

/* Probe every address on i2c0. */
void i2c_bus_scan(void);

/* Rescan the bus on an interval when the first scan found nothing, to tell a
 * slow-POR / intermittent-rail fault apart from a genuinely silent part. */
void i2c_rescan_watch(int seconds);

/* Bring-up diagnostics for the RV-3028-C7: deferred-init bind, ID/CLKOUT/time
 * register dump. Also completes the deferred device_init() dance for the
 * Zephyr rv3028 device handle if it hasn't bound yet. */
void rv3028_probe(void);

/* Read the seconds register directly (BCD -> binary), independent of the
 * rv3028.c driver. Returns -1 on I2C error. */
int rv3028_seconds(void);

#endif /* PERIPHERAL_RV3028_BRINGUP_H */
