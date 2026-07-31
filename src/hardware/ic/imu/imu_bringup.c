/* standard C */
#include <string.h>

/* Zephyr files */
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>

/* My header files */
#include <imu.h>
#include <imu_bringup.h>
#include <util/cdc_debug.h>

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

bool imu_alive;

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
int imu_raw_read_reg(uint8_t reg)
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

void imu_probe(void)
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
/* SN3 has the MT29F populated as of 2026-07-26 — probe re-enabled there. */
#define PROBE_NAND_CONTROL 1
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
        /* 2026-07-30: imu_init() fails intermittently across boots (~50%
         * observed via repeated resets), independent of APEX (reproduced
         * with IMU_APEX_ENABLED=0 too) — a real race somewhere in
         * init_icm()/imu_start()/imu_fifo_interrupt(), not yet root-caused.
         * Retrying a few times masks it well enough for unattended runs
         * (every failure seen in testing succeeded on the very next
         * attempt) without pretending it's fixed. imu_init() re-mallocs
         * imu_data_buffer every call, so free the previous one before each
         * retry or it leaks. */
        #define IMU_INIT_MAX_ATTEMPTS 4
        int rc = -1;

        for (int attempt = 1; attempt <= IMU_INIT_MAX_ATTEMPTS; attempt++) {
            if (attempt > 1 && imu_data_buffer != NULL) {
                circular_buffer_delete(imu_data_buffer);
                imu_data_buffer = NULL;
            }

            rc = imu_init();
            cdc_printf("  imu_init() attempt %d/%d -> %d (%s)\r\n",
                       attempt, IMU_INIT_MAX_ATTEMPTS, rc,
                       rc == 0 ? "OK" : "FAILED");

            if (rc == 0) {
                break;
            }

            k_msleep(50);
        }

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
