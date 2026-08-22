/* Zephyr files */
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/i2c.h>

#include <util/cdc_debug.h>
#include <peripheral/rv3028_bringup.h>

static const struct device *const i2c_dev =
    DEVICE_DT_GET(DT_NODELABEL(i2c0));

static const struct device *const rtc_dev =
    DEVICE_DT_GET(DT_NODELABEL(rv3028));

#define RTC_SETTLE_TIMEOUT_MS  2000
#define RV3028_REG_STATUS      0x0E
#define RV3028_STATUS_EEBUSY   BIT(7)

/* RV-3028-C7 register map (subset) */
#define RV3028_REG_SECONDS 0x00
#define RV3028_REG_CLKOUT  0x35
#define RV3028_REG_ID      0x28
#define RV3028_REG_BACKUP  0x37

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

/* Report what the SoC's LF clock actually settled on. Zephyr selects the source
 * at PRE_KERNEL_2; if the external clock never appears it can fall back or hang,
 * so read the hardware rather than trusting the Kconfig choice.
 * LFCLKSTAT: bits[1:0] SRC (0=RC, 1=Xtal, 2=Synth), bit16 STATE (1=running). */
#define NRF_CLOCK_LFCLKSTAT 0x40000418
#define NRF_CLOCK_LFCLKSRC  0x40000518

void lfclk_report(void)
{
    uint32_t stat = *(volatile uint32_t *)NRF_CLOCK_LFCLKSTAT;
    uint32_t src = *(volatile uint32_t *)NRF_CLOCK_LFCLKSRC;
    const char *srcname;

    switch (stat & 0x3) {
    case 0:  srcname = "RC";    break;
    case 1:  srcname = "Xtal";  break;
    case 2:  srcname = "Synth"; break;
    default: srcname = "?";     break;
    }

    cdc_printf("LFCLKSTAT=0x%08x src=%s running=%d  LFCLKSRC=0x%08x\r\n",
               stat, srcname, (stat >> 16) & 1u, src);
}

/* Probe every address on i2c0. NOTE: the RV-3028 at 0x52 is currently the
 * only IC populated on this bus — the mcp23008@20 node in the DTS is not
 * fitted on any board, so its absence from the scan is expected and there is
 * no second device to use as a bus-health control. */
/* When the initial scan comes up empty, the useful follow-up question is
 * "empty forever, or just not yet?". A part that is slow to leave POR, or one
 * flipping in and out of RV-3028 backup mode as a floating rail drifts, both
 * look identical to a single scan at t=0 but are very different faults from a
 * dead chip. Rescan on an interval and report the first address seen and when.
 * Read-only; only runs when the first scan found nothing. */
void i2c_rescan_watch(int seconds)
{
    uint8_t dummy;
    int64_t t0 = k_uptime_get();

    cdc_printf("  [rescan] nothing ACKed at t=0 — watching the bus for %ds\r\n",
               seconds);

    while ((k_uptime_get() - t0) < (seconds * 1000)) {
        for (uint8_t addr = 0x08; addr <= 0x77; addr++) {
            if (i2c_read(i2c_dev, &dummy, 1, addr) == 0) {
                cdc_printf("  [rescan] 0x%02x ACKed at t=%lldms%s\r\n",
                           addr, k_uptime_get() - t0,
                           addr == 0x52 ? "  (RV-3028)" : "");
                return;
            }
        }
        k_msleep(1000);
    }

    cdc_printf("  [rescan] still silent after %ds — not a slow-POR or an\r\n"
               "           intermittent rail; the part is not answering at all\r\n",
               seconds);
}

void i2c_bus_scan(void)
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

    /* When nothing ACKs, the next question is always "dead bus or dead device?"
     * and guessing wrong sends someone to the soldering iron for nothing. Read
     * the idle line levels: I2C is open-drain with external pull-ups, so both
     * lines must sit HIGH when the bus is idle. A line stuck LOW is an
     * electrical fault (short, or a device clock-stretching/holding SDA);
     * both HIGH means the wiring and pull-ups are fine and the device itself
     * is simply not answering — solder, power, or a dead part.
     * SCL = P0.30, SDA = P1.09 (i2c0_default in nrf52833_ders-pinctrl.dtsi). */
    {
        uint32_t p0_in = *(volatile uint32_t *)0x50000510;
        uint32_t p1_in = *(volatile uint32_t *)0x50000810;
        int scl = (p0_in >> 30) & 1;
        int sda = (p1_in >>  9) & 1;

        cdc_printf("  idle lines: SCL(P0.30)=%d SDA(P1.09)=%d -> %s\r\n",
                   scl, sda,
                   (scl && sda)
                       ? "bus OK (pull-ups fine) — device is silent, not the bus"
                       : "BUS FAULT — a line is held LOW, check shorts/stuck device");

        /* Healthy-but-empty bus is the ambiguous case worth spending time on. */
        if (found == 0 && scl && sda) {
            i2c_rescan_watch(20);
        }
    }
}

void rv3028_probe(void)
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

    /* CLKOUT config as actually programmed (EEPROM-backed): bit7 = CLKOE,
     * bits2:0 = FD (0 = 32768 Hz, 7 = pin held LOW). */
    {
        uint8_t clkout;

        if (i2c_reg_read_byte(i2c_dev, 0x52, RV3028_REG_CLKOUT, &clkout) == 0) {
            cdc_printf("CLKOUT reg 0x35 = 0x%02x (CLKOE=%d, FD=%d)\r\n",
                       clkout, (clkout & 0x80) ? 1 : 0, clkout & 0x07);
        } else {
            cdc_write("CLKOUT reg 0x35 read FAILED\r\n");
        }
    }

    /* BACKUP (EEPROM-backed): bits 3:2 = BSM. 3 = LEVEL (LSM), 1 = DIRECT (DSM),
     * 0 = disabled. VBACKUP is wired to VBAT here, which sits above the 3.3 V
     * VDD for most of the battery range — under DSM that parks the part in
     * backup mode, where it powers down I2C and never ACKs. LEVEL is the only
     * correct setting on this board, so print it: the DTS property alone does
     * nothing, the driver has to have landed the write over I2C. */
    {
        uint8_t backup;

        if (i2c_reg_read_byte(i2c_dev, 0x52, RV3028_REG_BACKUP, &backup) == 0) {
            uint8_t bsm = (backup >> 2) & 0x03;

            cdc_printf("BACKUP reg 0x37 = 0x%02x (BSM=%d, %s)\r\n",
                       backup, bsm,
                       bsm == 3 ? "LEVEL/LSM — correct for VBACKUP=VBAT" :
                       bsm == 1 ? "DIRECT/DSM — WRONG, I2C will die on battery" :
                                  "disabled");
        } else {
            cdc_write("BACKUP reg 0x37 read FAILED\r\n");
        }
    }

    ret = i2c_burst_read(i2c_dev, 0x52, RV3028_REG_SECONDS, regs, sizeof(regs));
    if (ret != 0) {
        cdc_printf("time regs read FAILED (%d)\r\n", ret);
        return;
    }
    cdc_printf("regs 0x00-0x06 (BCD): %02x %02x %02x %02x %02x %02x %02x\r\n",
               regs[0], regs[1], regs[2], regs[3], regs[4], regs[5], regs[6]);
}

/* Read the seconds register (BCD -> binary). Returns -1 on I2C error. */
int rv3028_seconds(void)
{
    uint8_t s;

    if (i2c_reg_read_byte(i2c_dev, 0x52, RV3028_REG_SECONDS, &s) != 0) {
        return -1;
    }
    return ((s >> 4) & 0x07) * 10 + (s & 0x0f);
}
