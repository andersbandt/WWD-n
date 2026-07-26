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
#include <zephyr/drivers/spi.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/logging/log.h>
#include <zephyr/usb/usb_device.h>

/* IMU bring-up */
#include <imu.h>
#include <ICM_42670.h>   /* getDataFromFifo() */

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
#define RV3028_REG_CLKOUT  0x35
#define RV3028_REG_ID      0x28


/* Write straight to the CDC endpoint. printk() cannot be used here: with
 * CONFIG_LOG_PRINTK=y it is routed into the log subsystem, which during
 * bring-up only has the RTT backend, so it never reaches USB. */
static void cdc_write(const char *s)
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

static void cdc_printf(const char *fmt, ...)
{
    char buf[160];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    cdc_write(buf);
}

/* Report what the SoC's LF clock actually settled on. Zephyr selects the source
 * at PRE_KERNEL_2; if the external clock never appears it can fall back or hang,
 * so read the hardware rather than trusting the Kconfig choice.
 * LFCLKSTAT: bits[1:0] SRC (0=RC, 1=Xtal, 2=Synth), bit16 STATE (1=running). */
#define NRF_CLOCK_LFCLKSTAT 0x40000418
#define NRF_CLOCK_LFCLKSRC  0x40000518

static void lfclk_report(void)
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

/* ---------------------------------------------------------------------------
 * ICM-42670-P bring-up (SN2, freshly soldered)
 *
 * Two stages, deliberately separate:
 *   1. A raw WHO_AM_I read that does not touch the InvenSense driver at all.
 *      This answers the only question that matters first — is the part
 *      soldered, powered, and talking on SPI1 — without any of the driver's
 *      reset/MCLK sequencing in the way.
 *   2. The full imu_init(), only if stage 1 says the part is alive.
 *
 * NVS is not running in this harness, so the known MT29F-holds-MISO bus
 * contention (see imu_notes.md) is out of the picture. The NAND CS idles high
 * and it has never been talked to this boot.
 * ------------------------------------------------------------------------- */
#define ICM_WHO_AM_I_REG   0x75
#define ICM_WHOAMI_EXPECT  0x67

/* Set once imu_init() succeeds, so the heartbeat only reads sensor data from a
 * part that is actually up. */
static bool imu_alive;

/* IMU_INT1 — P0.09, int-gpios on the icm42670p node. INT2 is not wired on this
 * hardware, so INT1 is the only interrupt line available. */
static const struct gpio_dt_spec imu_int1 =
    GPIO_DT_SPEC_GET(DT_NODELABEL(icm42670p), int_gpios);

static struct gpio_callback imu_int1_cb_data;
static volatile uint32_t imu_int1_count;

static void imu_int1_handler(const struct device *port,
                             struct gpio_callback *cb, gpio_port_pins_t pins)
{
    ARG_UNUSED(port);
    ARG_UNUSED(cb);
    ARG_UNUSED(pins);

    imu_int1_count++;
}

/* Attach the MCU-side edge interrupt. The IMU-side routing (FIFO_THS -> INT1
 * pin, push-pull, active low) is done in enableFifoInterrupt(). */
static int imu_int1_setup(void)
{
    int rc;

    if (!gpio_is_ready_dt(&imu_int1)) {
        cdc_write("  INT1 gpio not ready\r\n");
        return -ENODEV;
    }

    rc = gpio_pin_configure_dt(&imu_int1, GPIO_INPUT);
    if (rc != 0) {
        cdc_printf("  INT1 configure failed: %d\r\n", rc);
        return rc;
    }

    /* GPIO_INT_EDGE_TO_ACTIVE respects the ACTIVE_LOW flag in the DTS, so this
     * is a falling edge on the wire — matching INT_CONFIG_INT1_POLARITY_LOW. */
    rc = gpio_pin_interrupt_configure_dt(&imu_int1, GPIO_INT_EDGE_TO_ACTIVE);
    if (rc != 0) {
        cdc_printf("  INT1 interrupt configure failed: %d\r\n", rc);
        return rc;
    }

    gpio_init_callback(&imu_int1_cb_data, imu_int1_handler, BIT(imu_int1.pin));
    gpio_add_callback(imu_int1.port, &imu_int1_cb_data);

    cdc_printf("  INT1 armed on P0.%02u (edge-to-active), idle level = %d\r\n",
               imu_int1.pin, gpio_pin_get_dt(&imu_int1));
    return 0;
}

static const struct spi_dt_spec imu_spi =
    SPI_DT_SPEC_GET(DT_NODELABEL(icm42670p),
                    SPI_OP_MODE_MASTER | SPI_WORD_SET(8) | SPI_LINES_SINGLE,
                    0);

/* Single-byte register read, MSB set = read. Returns the byte, or -errno. */
static int imu_raw_read_reg(uint8_t reg)
{
    uint8_t tx[2] = { reg | 0x80, 0x00 };
    uint8_t rx[2] = { 0, 0 };
    struct spi_buf tx_b = { .buf = tx, .len = sizeof(tx) };
    struct spi_buf rx_b = { .buf = rx, .len = sizeof(rx) };
    struct spi_buf_set tx_s = { .buffers = &tx_b, .count = 1 };
    struct spi_buf_set rx_s = { .buffers = &rx_b, .count = 1 };
    int rc = spi_transceive_dt(&imu_spi, &tx_s, &rx_s);

    return (rc != 0) ? rc : rx[1];
}

/* Bus-health control: the MT29F NAND sits on the same SCK/MOSI/MISO net as the
 * IMU, on its own CS (P0.20). If the NAND answers READ ID but the IMU does not,
 * the shared bus wiring and the MISO return path are proven good and the fault
 * is local to the IMU — its CS (P0.10), its supply, or its solder joints.
 * SPI mode 3 here, matching the NAND (frame-format 0 in the DTS). */
static const struct spi_dt_spec nand_spi =
    SPI_DT_SPEC_GET(DT_NODELABEL(mt29f),
                    SPI_OP_MODE_MASTER | SPI_WORD_SET(8) | SPI_LINES_SINGLE |
                        SPI_MODE_CPOL | SPI_MODE_CPHA,
                    0);

static void nand_id_probe(void)
{
    uint8_t tx[4] = { 0x9F, 0x00, 0x00, 0x00 };  /* READ ID + dummy + 2 data */
    uint8_t rx[4] = { 0, 0, 0, 0 };
    struct spi_buf tx_b = { .buf = tx, .len = sizeof(tx) };
    struct spi_buf rx_b = { .buf = rx, .len = sizeof(rx) };
    struct spi_buf_set tx_s = { .buffers = &tx_b, .count = 1 };
    struct spi_buf_set rx_s = { .buffers = &rx_b, .count = 1 };
    int rc;

    if (!spi_is_ready_dt(&nand_spi)) {
        cdc_write("  [bus control] nand spi not ready\r\n");
        return;
    }

    rc = spi_transceive_dt(&nand_spi, &tx_s, &rx_s);
    cdc_printf("  [bus control] MT29F READ ID rc=%d -> %02x %02x "
               "(expect 2c 24) %s\r\n",
               rc, rx[2], rx[3],
               (rx[2] == 0x2c && rx[3] == 0x24)
                   ? "=> SPI1 bus + MISO path GOOD, fault is IMU-local"
                   : "=> NAND silent too, suspect the shared bus");
}

/* SPIM1 register dump. Read from firmware rather than GDB: gdb_query resets the
 * target on connect, which would wipe the very configuration being inspected.
 * PSEL encoding: bit31 = DISCONNECTED, bit5 = port, bits[4:0] = pin. */
#define SPIM1_BASE 0x40004000

static void spim_reg(const char *name, uint32_t off)
{
    uint32_t v = *(volatile uint32_t *)(SPIM1_BASE + off);

    cdc_printf("    %-10s @0x%03x = 0x%08x\r\n", name, off, v);
}

static void spim_psel(const char *name, uint32_t off)
{
    uint32_t v = *(volatile uint32_t *)(SPIM1_BASE + off);

    if (v & BIT(31)) {
        cdc_printf("    %-10s = DISCONNECTED (0x%08x)\r\n", name, v);
    } else {
        cdc_printf("    %-10s = P%u.%02u\r\n", name, (v >> 5) & 1u, v & 0x1fu);
    }
}

static void spim_dump(void)
{
    cdc_write("  [spim1] peripheral state after the transfers above:\r\n");
    spim_reg("ENABLE",   0x500);   /* 7 = SPIM enabled */
    spim_psel("PSEL.SCK",  0x508);
    spim_psel("PSEL.MOSI", 0x50C);
    spim_psel("PSEL.MISO", 0x510);
    spim_reg("FREQUENCY", 0x524);
    spim_reg("RXD.AMOUNT", 0x53C); /* bytes actually clocked in on the last xfer */
    spim_reg("TXD.AMOUNT", 0x54C);
    spim_reg("CONFIG",    0x554);
}

/* Pin integrity test. Drive each SPI1 pin as an input with an internal pull and
 * read it back. With nothing external driving, the pin must follow the pull:
 *   pull-up -> 1, pull-down -> 0  = pin is free and healthy
 *   stuck 0 with pull-up          = shorted to GND (solder bridge)
 *   stuck 1 with pull-down        = shorted to a supply
 *
 * This is how the dead IMU chip select was found (P0.10 read stuck low), but it
 * is DESTRUCTIVE: rewriting PIN_CNF hands SCK/MOSI/MISO to the GPIO block, and
 * SPIM cannot drive them afterwards. Every SPI transaction that follows returns
 * garbage. Only ever call it after all SPI work is finished — running it early
 * silently invalidated a long run of measurements here. */
static void pin_integrity_test(void)
{
    static const struct {
        uint8_t pin;
        const char *name;
    } pins[] = {
        { 31, "SCK  P0.31" },
        { 17, "MOSI P0.17" },
        { 15, "MISO P0.15" },
        { 10, "IMU_CS  P0.10" },
        { 20, "NAND_CS P0.20" },
    };
    const struct device *g0 = DEVICE_DT_GET(DT_NODELABEL(gpio0));

    cdc_write("  [pins] internal pull-up/pull-down readback:\r\n");

    if (!device_is_ready(g0)) {
        cdc_write("    gpio0 not ready\r\n");
        return;
    }

    for (size_t i = 0; i < ARRAY_SIZE(pins); i++) {
        int up, down;

        gpio_pin_configure(g0, pins[i].pin, GPIO_INPUT | GPIO_PULL_UP);
        k_busy_wait(200);
        up = gpio_pin_get_raw(g0, pins[i].pin);

        gpio_pin_configure(g0, pins[i].pin, GPIO_INPUT | GPIO_PULL_DOWN);
        k_busy_wait(200);
        down = gpio_pin_get_raw(g0, pins[i].pin);

        cdc_printf("    %-14s pu=%d pd=%d  %s\r\n", pins[i].name, up, down,
                   (up == 1 && down == 0) ? "free/healthy" :
                   (up == 0 && down == 0) ? "STUCK LOW — shorted to GND?" :
                   (up == 1 && down == 1) ? "STUCK HIGH — shorted to VDD?" :
                                            "inverted?!");
    }
}

/* Register read with a caller-chosen second (dummy) byte. On a read the IMU
 * ignores byte 2 entirely, so this is functionally identical to imu_raw_read_reg
 * — except that it puts arbitrary data on MOSI during the second byte, exactly
 * as a write does. It isolates "byte 2 carries data" from "byte 1 has its MSB
 * clear" as the thing that kills the part. */
static int imu_raw_read_reg_dummy(uint8_t reg, uint8_t dummy)
{
    uint8_t tx[2] = { reg | 0x80, dummy };
    uint8_t rx[2] = { 0, 0 };
    struct spi_buf tx_b = { .buf = tx, .len = sizeof(tx) };
    struct spi_buf rx_b = { .buf = rx, .len = sizeof(rx) };
    struct spi_buf_set tx_s = { .buffers = &tx_b, .count = 1 };
    struct spi_buf_set rx_s = { .buffers = &rx_b, .count = 1 };
    int rc = spi_transceive_dt(&imu_spi, &tx_s, &rx_s);

    return (rc != 0) ? rc : rx[1];
}

/* Single-byte register write, MSB clear = write. */
static int imu_raw_write_reg(uint8_t reg, uint8_t val)
{
    uint8_t tx[2] = { reg & 0x7f, val };
    struct spi_buf tx_b = { .buf = tx, .len = sizeof(tx) };
    struct spi_buf_set tx_s = { .buffers = &tx_b, .count = 1 };

    return spi_write_dt(&imu_spi, &tx_s);
}

/* Two questions the post-failure register dump cannot answer on its own:
 *
 *  1. Are the all-zero reads real, or an artefact? Every MREG access goes
 *     through BLK_SEL_R/BLK_SEL_W. If init_hardware_from_ui() aborts midway
 *     (e.g. the MCLK poll times out) it leaves BLK_SEL_R pointing at an MREG
 *     bank, and every later bank-0 read returns 0x00 even though the part is
 *     perfectly healthy. Clearing BLK_SEL_R and re-reading WHO_AM_I settles it.
 *
 *  2. Does MCLK actually come ready? That poll is the driver's prime suspect,
 *     so drive it by hand: set PWR_MGMT0.IDLE, then watch MCLK_RDY bit 0.
 */
#define REG_MCLK_RDY   0x00
#define REG_PWR_MGMT0  0x1f
#define REG_BLK_SEL_W  0x79
#define REG_BLK_SEL_R  0x7c

static void imu_post_mortem(void)
{
    int who, pwr, i;

    cdc_write("  [post-mortem] clearing BLK_SEL_R/W, then re-reading:\r\n");
    imu_raw_write_reg(REG_BLK_SEL_W, 0x00);
    imu_raw_write_reg(REG_BLK_SEL_R, 0x00);
    k_msleep(1);

    who = imu_raw_read_reg(ICM_WHO_AM_I_REG);
    cdc_printf("    WHO_AM_I = 0x%02x  %s\r\n", who & 0xff,
               who == ICM_WHOAMI_EXPECT
                   ? "=> part is FINE; the 0x00s were a stale MREG bank"
                   : "=> still dead, part really did stop responding");

    if (who != ICM_WHOAMI_EXPECT) {
        /* Try a manual soft reset (SIGNAL_PATH_RESET.SOFT_RESET_DEVICE_CONFIG,
         * bit 4). If the part comes back, whatever the driver programmed is
         * recoverable configuration rather than a wedged or damaged device. */
        cdc_write("    attempting manual soft reset...\r\n");
        imu_raw_write_reg(0x02, BIT(4));
        k_msleep(50);
        who = imu_raw_read_reg(ICM_WHO_AM_I_REG);
        cdc_printf("    WHO_AM_I after soft reset = 0x%02x  %s\r\n", who & 0xff,
                   who == ICM_WHOAMI_EXPECT
                       ? "=> RECOVERED; driver config wedged it, not the HW"
                       : "=> still no response");

        /* Re-run the pin test: this distinguishes a part that has stopped
         * driving MISO (pull-up wins, pu=1) from one actively holding it low
         * (pu=0) — the latter means the driver switched the interface into a
         * mode where MISO is no longer the SPI read line. Runs last because it
         * takes the pins away from SPIM. */
        pin_integrity_test();
        return;
    }

    /* Manual MCLK bring-up, mirroring inv_imu_switch_on_mclk(). */
    pwr = imu_raw_read_reg(REG_PWR_MGMT0);
    imu_raw_write_reg(REG_PWR_MGMT0, (uint8_t)(pwr | BIT(4)));  /* IDLE */
    cdc_printf("    PWR_MGMT0 %02x -> %02x (IDLE set)\r\n",
               pwr & 0xff, imu_raw_read_reg(REG_PWR_MGMT0) & 0xff);

    for (i = 0; i < 20; i++) {
        int rdy = imu_raw_read_reg(REG_MCLK_RDY);

        if (rdy & 0x01) {
            cdc_printf("    MCLK_RDY = 0x%02x after %d ms => MCLK COMES UP "
                       "FINE by hand\r\n", rdy & 0xff, i);
            return;
        }
        k_msleep(1);
    }
    cdc_printf("    MCLK_RDY never set (last 0x%02x) => MCLK is the blocker\r\n",
               imu_raw_read_reg(REG_MCLK_RDY) & 0xff);
}

static void imu_probe(void)
{
    int who = -1;

    cdc_write("\r\n===== ICM-42670-P probe =====\r\n");

    if (!spi_is_ready_dt(&imu_spi)) {
        cdc_write("spi1 NOT ready — bus driver failed to init\r\n");
        return;
    }

    /* Read a few times: a single 0x00 or 0xFF is ambiguous (bus stuck low vs
     * MISO floating high), but a stable repeated 0x67 is unambiguous life. */
    for (int i = 0; i < 3; i++) {
        who = imu_raw_read_reg(ICM_WHO_AM_I_REG);
        cdc_printf("  raw WHO_AM_I[%d] = 0x%02x (%s)\r\n", i, who & 0xff,
                   who == ICM_WHOAMI_EXPECT ? "OK" :
                   who == 0x00 ? "MISO stuck low / no part" :
                   who == 0xff ? "MISO floating high" : "unexpected");
        k_msleep(5);
    }

    /* The MT29F is NOT POPULATED on this board, so it is useless as a bus-health
     * control and its 00 00 "reply" was just an undriven bus. It also means the
     * IMU is the only possible driver of MISO — every MISO reading below is the
     * IMU's doing, and none of the NAND contention in imu_notes.md applies here. */
#define PROBE_NAND_CONTROL 0
#if PROBE_NAND_CONTROL
    nand_id_probe();
#else
    cdc_write("  [bus control] MT29F probe skipped (see PROBE_NAND_CONTROL)\r\n");
#endif

    /* Re-read after the NAND transaction. The ICM-42670 latches its interface
     * choice on early bus activity, and a marginal joint can behave differently
     * once the bus has been exercised — cheap to check, and it also confirms
     * the IMU result is not an artefact of being the very first transfer. */
    who = imu_raw_read_reg(ICM_WHO_AM_I_REG);
    cdc_printf("  raw WHO_AM_I after NAND = 0x%02x\r\n", who & 0xff);

    spim_dump();

    if (who != ICM_WHOAMI_EXPECT) {
        cdc_write("  part not responding — skipping imu_init()\r\n");
        cdc_write("=============================\r\n");
        return;
    }

    /* Read DEVICE_CONFIG for the record. It powers up at 0x04, which is exactly
     * SPI_4WIRE|SPI_MODE_0_3 — the value configure_serial_interface() writes —
     * so the driver's bit positions for this register are correct and the
     * interface mode is not being changed out from under us. */
    cdc_printf("  DEVICE_CONFIG (0x01) = 0x%02x (expect 0x04 = 4-wire, mode0/3)"
               "\r\n", imu_raw_read_reg(0x01) & 0xff);

    /* Sanity-check that reads in general return plausible data, not just the one
     * register that happens to have a known-good constant in it. Two different
     * registers both reading back 0x2b earlier was suspicious enough to check. */
    {
        static const uint8_t regs[] = { 0x01, 0x1f, 0x29, 0x2b, 0x35, 0x3a, 0x75 };

        cdc_write("  [read sweep] ");
        for (size_t i = 0; i < ARRAY_SIZE(regs); i++) {
            cdc_printf("%02x=%02x ", regs[i], imu_raw_read_reg(regs[i]) & 0xff);
        }
        cdc_write("\r\n");
    }


    /* Write-integrity test.
     *
     * On a read only the first MOSI byte (the address) has to be correct — the
     * second byte is dummy clocks and its value is ignored. On a write the
     * second byte carries the data. Reads here are 100% reliable while the
     * first write is always fatal, which is exactly what a corrupted *second*
     * MOSI byte would look like. It would also explain the otherwise absurd
     * result that writing a register the value it already held still killed the
     * part: that write did not store the same value, it stored garbage into an
     * interface-config register.
     *
     * FIFO_CONFIG2 (0x29) is a plain 8-bit FIFO watermark byte — fully
     * read/write with no effect on the serial interface, so it is safe to
     * scribble on. Write a known pattern and read it straight back:
     *   readback == written  -> the write path is clean, look elsewhere
     *   readback != written  -> MOSI byte 2 is corrupt (hardware)
     */
#define REG_FIFO_CONFIG2 0x29
#define RUN_WRITE_TEST 0
#if RUN_WRITE_TEST
    {
        static const uint8_t patterns[] = { 0xa5, 0x5a, 0x0f };

        cdc_write("  [write test] FIFO_CONFIG2 (0x29), benign scratch reg:\r\n");
        cdc_printf("    initial value = 0x%02x\r\n",
                   imu_raw_read_reg(REG_FIFO_CONFIG2) & 0xff);

        for (size_t i = 0; i < ARRAY_SIZE(patterns); i++) {
            int back, who_now;

            imu_raw_write_reg(REG_FIFO_CONFIG2, patterns[i]);
            k_msleep(2);
            back = imu_raw_read_reg(REG_FIFO_CONFIG2);
            who_now = imu_raw_read_reg(ICM_WHO_AM_I_REG);

            cdc_printf("    wrote 0x%02x -> read 0x%02x %s | WHO_AM_I 0x%02x\r\n",
                       patterns[i], back & 0xff,
                       (back == patterns[i]) ? "MATCH  " : "MISMATCH",
                       who_now & 0xff);

            if (who_now != ICM_WHOAMI_EXPECT) {
                cdc_write("    part stopped responding — aborting write test\r\n");
                break;
            }
        }
    }
#endif /* RUN_WRITE_TEST */

    /* imu_init() gets a clean bus: everything above is reads only, and
     * pin_integrity_test() is deliberately deferred to after this point. That
     * test rewrites PIN_CNF on SCK/MOSI/MISO to make them pulled inputs, which
     * takes them away from SPIM — running it beforehand corrupted every
     * transaction that followed and invalidated earlier measurements here. */
    {
        int rc = imu_init();

        cdc_printf("  imu_init() -> %d (%s)\r\n", rc,
                   rc == 0 ? "OK" : "FAILED");
        imu_alive = (rc == 0);

        if (imu_alive) {
            cdc_printf("  INT_CONFIG (0x06) = 0x%02x, INT_SOURCE0 (0x2b) = "
                       "0x%02x (bit2 = FIFO_THS->INT1)\r\n",
                       imu_raw_read_reg(0x06) & 0xff,
                       imu_raw_read_reg(0x2b) & 0xff);
            imu_int1_setup();
        }

        /* inv_imu_init() OR-accumulates its stage results and the driver's
         * per-stage detail only goes to LOG_DBG over RTT, so a bare -1 says
         * nothing about which stage failed. Dump the registers the init path
         * actually keys off, straight to the port we can read.
         * Bank-0 addresses = low byte of the driver's 0x1xxxx encoding. */
        if (rc != 0) {
            cdc_write("  post-failure register state:\r\n");
            cdc_printf("    WHO_AM_I   (0x75) = 0x%02x\r\n",
                       imu_raw_read_reg(0x75) & 0xff);
            cdc_printf("    MCLK_RDY   (0x00) = 0x%02x  (bit0 = MCLK ready)\r\n",
                       imu_raw_read_reg(0x00) & 0xff);
            cdc_printf("    PWR_MGMT0  (0x1f) = 0x%02x  (bit4 = IDLE)\r\n",
                       imu_raw_read_reg(0x1f) & 0xff);
            cdc_printf("    INT_STATUS (0x3a) = 0x%02x  (bit4 = RESET_DONE)\r\n",
                       imu_raw_read_reg(0x3a) & 0xff);
            imu_post_mortem();
        }
    }
    cdc_write("=============================\r\n");
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
    lfclk_report();
    if (!device_is_ready(i2c_dev)) {
        cdc_write("i2c0 NOT ready — bus driver failed to init\r\n");
    } else {
        i2c_bus_scan();
        rv3028_probe();
    }
    cdc_write("==================================\r\n");

    imu_probe();

    while (1) {
        int secs = rv3028_seconds();

        dc_level = !dc_level;
        gpio_pin_set_dt(&disp_dc, dc_level);

        if (secs < 0) {
            cdc_write("... heartbeat ...  rtc: I2C ERR");
        } else {
            cdc_printf("... heartbeat ...  rtc secs: %02d", secs);
        }

        /* Live accel readout, so the IMU is shown actually streaming data rather
         * than merely having initialised. ACCEL_DATA_X1/X0 at 0x0b..0x10 are
         * big-endian signed 16-bit pairs; tilt the board and these move. */
        if (imu_alive) {
            int16_t ax = (int16_t)((imu_raw_read_reg(0x0b) << 8) |
                                   (imu_raw_read_reg(0x0c) & 0xff));
            int16_t ay = (int16_t)((imu_raw_read_reg(0x0d) << 8) |
                                   (imu_raw_read_reg(0x0e) & 0xff));
            int16_t az = (int16_t)((imu_raw_read_reg(0x0f) << 8) |
                                   (imu_raw_read_reg(0x10) & 0xff));

            cdc_printf("   accel x=%6d y=%6d z=%6d", ax, ay, az);

            /* FIFO fill level. NB the byte order is the opposite of what the
             * regmap names suggest: 0x3d (named FIFO_COUNTH) empirically holds
             * the LOW byte and 0x3e the high byte. Reading it the documented way
             * gave an impossible 16384 for a 2 KB FIFO. Against a watermark of
             * IMU_FIFO_WM (10) this sits at 11-12, as expected. */
            {
                uint16_t fifo_count =
                    (uint16_t)((imu_raw_read_reg(0x3e) << 8) |
                               (imu_raw_read_reg(0x3d) & 0xff));

                cdc_printf("   fifo=%3u  INT1 edges=%u",
                           fifo_count, imu_int1_count);
            }
        }
        cdc_write("\r\n");

        /* Causality check. With WM_GT_TH_EN cleared the watermark fires only on
         * count == threshold exactly, so it re-arms once per drain: draining at
         * 10 Hz must give ~10 edges/sec, versus ~1/sec when draining once per
         * heartbeat. If the rate tracks the drain rate the edges really are
         * watermark-driven; if it stays pinned to the 1 Hz print, they are an
         * artefact of this loop's own SPI traffic. */
        for (int i = 0; i < 10; i++) {
            if (imu_alive) {
                getDataFromFifo();
            }
            k_msleep(100);
        }
    }

    return 0;
}
