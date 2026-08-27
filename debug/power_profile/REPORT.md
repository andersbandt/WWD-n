# WWD-n Power Profiling Report

**Board:** SN1 (`5C017BA8601A3781`) · **Date:** 2026-08-24 · **Firmware:** `dev` @ `e13f8aa`

Everything below was measured on hardware. Where a number is inferred rather than
measured, it says so. Open questions are listed at the end rather than papered over.

---

## 1. Bench setup (reproduce this exactly)

| Item | Value |
|---|---|
| Supply | Siglent SPD3303X, **channel 2**, feeding the **battery port** |
| VISA address | `USB0::62700::5168::SPD1A134600512::0::INSTR` |
| Current sense | **1.6 Ω shunt inline** on the battery-port feed |
| Meter | OWON XDM1041 across the shunt, **mV mode, low sample speed** |
| Meter port | `/dev/ttyUSB0` |
| Resolution | **~10 µA** (vs ~1 mA for PS telemetry) |
| Burden | ~11 mV at 7 mA — negligible against 3.3 V |

The DMM is **not** in ammeter mode. It measures the voltage across a shunt, so:

```
I  = V_shunt / 1.6
V_board = PS_Vmeas1 - V_shunt      # subtract the burden
```

### Logging

```python
log_start(use_dmm=True, use_ps=True, ps_channel=2, interval_s=0.2,
          math_columns=["I_A = DMM_Meas1 / 1.6",
                        "V_board = PS_Vmeas1 - DMM_Meas1"])
```
```bash
python3 analyze_power_profile.py <csv> --t0-offset <fitted> \
    --current-col I_A --voltage-col V_board --settle-s 5.0 --edge-trim-s 0.5
```

### Hard rules

- **USB must be unplugged.** With USB attached the battery-port supply reads **0.000 A while
  the device runs** — VBUS back-feeds VBAT through a hardware defect. Any USB-attached
  measurement characterises the defect, not the firmware.
- **The J-Link may stay attached.** Measured attached vs physically detached: identical
  (`bare_idle` 2.73 mA both). It costs nothing, so register inspection and current
  measurement can happen at the same time.
- **t=0 is a PS power-cycle**, not `nrfjprog -r`. The firmware runs its sequence autonomously;
  the debugger is only needed to flash.
- `nrfjprog --memrd` **halts the CPU and leaves it halted.** Follow every read with
  `nrfjprog --run`, or the firmware never advances again. Polling in a loop never converges.

### Known measurement caveats

- **PS setpoint readback runs ~0.13–0.15 V above commanded.** With the single DMM occupied by
  the shunt there is no independent rail measurement, so the absolute voltage axis carries
  that error. The brownout point is either 2.85 V or 3.02 V depending on which is true.
- `PS_Imeas1` means *first logged channel*, **not** PS channel 1. With `ps_channel=2` the
  `PS_*1` columns carry channel 2. Do not "fix" this to `PS_*2`.
- `log_start` timestamps have **1-second resolution even at `interval_s=0.2`**, so samples
  near a state boundary can be misattributed. Hence `--edge-trim-s`.

---

## 2. Method

### Firmware images

| Build | Purpose |
|---|---|
| `-DPOWER_PROFILE_BUILD=ON` | 23-state scripted sequence, 8 s dwells |
| `-DPOWER_PROFILE_BOOST_HOLD=0\|1` | Parks the board quiet with BOOST_SEL held, for supply sweeps |
| `-DPOWER_PROFILE_TEARDOWN=ON` | Additive attribution: start with nothing initialised, add one subsystem per dwell |
| `-DPOWER_PROFILE_PROBE=ON` | Idle-floor (`bare_spin`) and gyro isolation |
| `-DEXTRA_CONF_FILE=debug/power_profile/tickless_on.conf` | Tickless A/B |
| `-DPOWER_PROFILE_BLE=ON` | BLE advertiser (scripted) + connection/transfer (host-driven) |

### Rules learned the hard way

1. **Fit `t0` from the trace every single run.** Power-on to schedule t=0 is ~5 s of Zephyr
   init plus a 2 s settle, and it varies run to run (7, 8, 9 s observed). Align on the
   backlight staircase. The confirmation you got it right: every state reports the same
   sample count.
2. **The 8 s dwell does not settle.** Every state ramps for ~6 s. Whole-window means carry
   ±1–3 mW; settled tails are ±0.00. Use `--settle-s 5.0 --edge-trim-s 0.5`.
3. **Sweep supply voltage DOWNWARD, always.** Sweeping up from below the brownout point
   silently corrupts every later point — the board latches off and the "readings" are leakage
   (~0.1–0.19 mA, rising with V like a resistor). Bin-and-average hides this completely.
4. **When neighbouring states differ hugely, read the per-second trace**, not the windowed
   mean. A large `±std` is the tell that a window straddled a transition.
5. **Do not infer clock state over SWD.** Reading `HFCLKSTAT` requires halting the CPU, and a
   halted CPU is awake, so HFCLK always reads "running". That reading is an artefact.

---

## 3. Results

### 3.1 Display and backlight — the dominant consumer

Settled, ~3.29 V:

| State | mA | mW |
|---|---|---|
| Panel asleep (`SLPIN`) | **2.67** | 8.81 |
| On, backlight 0% | 4.78 | 15.74 |
| On, 25% | 5.71 | 18.79 |
| On, 50% | 6.28 | 20.64 |
| On, 75% | 6.84 | 22.48 |
| On, 100% | 7.04 | 23.14 |

- **Panel logic alone costs ~2.1 mA**, before any light. The entire 0→100% backlight range is
  only ~2.3 mA more.
- The curve is **compressive**: 0→25% costs +0.93 mA, 75→100% only +0.20 mA.
- **Turning the panel off beats any dimming strategy.** Dimming to 0% still pays ~60% of the
  display's cost. `ST7735S_sleepIn()` works properly and is essentially free.

### 3.2 Battery divider

| State | mA |
|---|---|
| `VBAT_DIV_EN` off | 2.67 |
| on | **2.78** |
| off again | 2.67 |

**+0.11 mA (~110 µA)**, perfectly repeatable. Invisible on PS telemetry; clean on the shunt.

### 3.3 nRF52833 internal DC/DC

6.97 mA (on) vs 7.06 mA (off) — ~0.09 mA, and a repeat of the "on" state read 7.04. **The
effect is inside run-to-run repeatability**; negligible at this load. Confirm the DCC inductor
is populated before reading more into it.

### 3.4 IMU

| Step | mA | Δ |
|---|---|---|
| `icm_init_only` (WHO_AM_I only) | 2.12 | — |
| `accel_only` | 2.35 | **+0.22** |
| `accel_plus_gyro` | 2.69 | **+0.34 ← the gyro** |
| both at 800 Hz (8× the rate) | 2.74 | **+0.05** |

**This resolves the long-standing "IMU ODR does nothing" anomaly.** `imu_start()` and
`imu_set_odr()` both call `startGyro()` alongside `startAccel()`, each in **low-noise mode**
(`inv_imu_enable_gyro_low_noise_mode`). The ICM-42670-P gyro's drive circuit runs continuously
once enabled, so its cost does not scale with ODR.

- **Gyro = 0.34 mA**, ~13% of the idle floor and ~11× everything tickless would save. It is
  currently logged (`ICM_IS_GYRO_SUPPORTED 1`, packed into every `RECORD_IMU_FIFO`), so
  dropping it is a **product decision**, not free.
- **Higher IMU rate is nearly free** — 800 Hz costs 0.05 mA over 100 Hz. Do not economise on
  sample rate; economise on mode.

### 3.5 VBAT sweep, 2.85 → 4.50 V, both BOOST_SEL levels

Board idle, display off. Measured with the hold-mode images.

| V_board | BSEL=0 mA / mW | BSEL=1 mA / mW | Δ |
|---|---|---|---|
| 3.017 | 0.101 / 0.30 | 0.106 / 0.32 | both **dead** |
| 3.206 | 2.627 / 8.42 | 3.012 / 9.65 | +14.6% |
| 3.401 | 2.646 / 9.00 | 2.871 / 9.76 | +8.5% |
| 3.644 | 2.633 / 9.59 | 2.772 / 10.10 | +5.3% |
| 3.886 | 2.607 / 10.13 | 2.717 / 10.56 | +4.2% |
| 4.129 | 2.574 / 10.63 | 2.663 / 10.99 | +3.4% |
| 4.372 | 2.532 / 11.07 | 2.604 / 11.38 | +2.8% |
| 4.615 | 2.296 / 10.60 | 2.383 / 11.00 | +3.8% |

**Power RISES ~28% as the battery charges** (8.55 → 10.96 mW). A constant-power load would
give I ∝ 1/V (2.708 → ~1.98 mA); observed 2.708 → 2.535, much closer to constant *current*.
**The system is least efficient on a full battery** — the opposite of the usual assumption, and
it matters for runtime modelling.

### 3.6 BOOST_SEL — polarity and a hazard

**The `power.c` comment was backwards.** It documented active-HIGH as the lower "power-save"
rail. Measurement says HIGH costs **more** at every voltage, so **HIGH = the higher 3.0 V rail,
LOW = the cheaper 2.7 V rail.** `power_rail_init()` drives it LOW, so the board already boots
on the efficient rail.

Consequence: **`power_save_enable(true)` selects the MORE expensive rail.** The name is
inverted with respect to the hardware.

**HAZARD — brownout latch.** Both settings brown out at ~3.02 V. Recovery differs enormously:

| BOOST_SEL | Recovers at |
|---|---|
| 0 (low) | **3.21 V** |
| 1 (high) | **4.13 V** — measured dead at 3.21, 3.40, 3.65, 3.89 |

The MCP23008 keeps GP6 driven after the MCU browns out, so the converter stays in a mode that
cannot restart at low Vin. **On a battery this is a dead-device trap**: voltage only falls
further, so recovery needs a charger. Never wire a low-battery trigger to it.

### 3.7 Tickless kernel A/B (item 5)

Same image except the config flag; J-Link attached for both; ~3.29 V.

| State | tickless=n | tickless=y | Δ |
|---|---|---|---|
| `display_off` (idle floor) | 2.610 | 2.580 | **−0.030 (−1.1%)** |
| `batt_div_off` | 2.610 | 2.580 | −0.030 |
| `imu_odr_25…800` | 2.52–2.54 | 2.46–2.48 | −0.05/−0.06 |
| `display_bl_100` | 6.920 | 6.930 | +0.010 |

**~30–60 µA, about 1%.** The arithmetic never supported more: 128 wakeups/s × tens of µs of
tick ISR is well under 0.1% duty cycle. `prj.conf` sets `CONFIG_TICKLESS_KERNEL=n` to work
around a rare `k_msleep()`-never-wakes race; **a ~1% payoff does not justify reopening that.**

Functional check passed (all states on the 8 s cadence, staircase correct, no hang) but 3.5
minutes is **not** a soak test for a rare race.

### 3.8 Idle-floor attribution (teardown)

Additive — start with nothing initialised, add one subsystem per dwell:

| Step | mA | Δ |
|---|---|---|
| `bare_idle` (no init at all) | 2.733 | — |
| + `power_init()` | 2.118 | **−0.615** |
| + display, then `sleepIn()` | 2.105 | −0.013 |
| + `imu_init()` | 2.511 | +0.406 |
| + `nvs_init()` | 2.606 | +0.095 |
| + IMU ODR 100 Hz | 2.601 | −0.005 |
| display on, backlight 100% | 7.079 | +4.478 |

**`power_init()` SAVES 0.62 mA.** It drives BOOST_SEL to the cheap rail; before it runs, the
MCP23008 is at POR with all pins as inputs, so the mode select **floats into the expensive
mode**. This quantifies the BOOST_SEL drift theory: **an un-asserted BOOST_SEL costs 0.62 mA**,
and anything that resets the expander without re-asserting GP6 silently pays it.

### 3.9 What the floor is NOT

| Test | Result |
|---|---|
| J-Link attached vs detached | identical (2.73 mA both) — **not the debugger** |
| `bare_spin` (CPU busy-waits) vs `bare_idle` | 8.96 vs 2.88 mA — **+6.08 mA, so the CPU sleeps correctly** |
| Disable all EasyDMA + SAADC + USBD `ENABLE=0` | +0.04 mA = nothing — **not the peripherals** |
| Write `TWIM0 ENABLE=0` live | 2.899 → 2.899 mA — **no change** |

**~2.1 mA survives with no drivers, no peripherals, and a sleeping CPU.** Nothing in firmware
moves it.

---

### 3.10 Bluetooth LE — advertiser and data transfer (2026-08-25)

Measured after BLE went live (`4bda93a` / `3fc5500`), on SN1 at **3.839 V**, with the
`-DPOWER_PROFILE_BLE=ON` image: power rail + panel asleep only, **no IMU, no NAND, no
display**, so the radio deltas are not buried inside subsystems already characterised
above. The absolute floor here (1.94 mA) is therefore *lower* than the 2.67 mA product
floor in §3.1 — compare the deltas, not the floor.

Three states are scripted (30 s dwells); the connection phases cannot be, because the
central owns connect/subscribe/read, so the board holds advertising forever and the host
walks the phases against the same continuous log.

| State | mA | ±σ | mW |
|---|---|---|---|
| `ble_off` — `bt_enable()` never called | 1.939 | 0.023 | 7.45 |
| `ble_stack_up` — controller up, **not** advertising | 1.967 | 0.035 | 7.55 |
| `ble_adv` — advertising, `BT_LE_ADV_CONN` (100–150 ms) | 2.053 | 0.007 | 7.88 |
| Connected, idle (no subscription) | 2.033 | 0.002 | 7.80 |
| Connected + notify (19 B every 2 s) | 2.033 | 0.002 | 7.80 |
| Connected + sustained reads (9.7 reads/s) | 2.071 | 0.003 | 7.95 |
| Advertising again, post-disconnect | 2.061 | 0.005 | 7.91 |

Settled σ is 2–7 µA, so every delta below is far above the noise floor. The
post-disconnect state returning to within **8 µA** of the original `ble_adv` figure is
the repeatability check.

**1. The advertiser costs +0.114 mA.** That is ~4% of the 2.67 mA product idle floor —
about a third of what the gyro costs (0.34 mA), and 1/20th of the panel. Advertising is
not a power problem on this device, and slowing the advertising interval is not a lever
worth pulling.

**2. The user-facing BLE-off toggle saves +0.086 mA, not 0.114.** `ble_set_enabled(false)`
deliberately stops advertising rather than calling `bt_disable()`, so it leaves the
controller enabled. That residual — `ble_stack_up` vs `ble_off` — measured **+0.028 mA**,
and even that is at the edge: those two states sit on the boot thermal ramp (σ 0.023/0.035
vs 0.002–0.007 once settled), so read it as *≤0.03 mA, at the limit of resolution*. The
design tradeoff in `ble.h` (keep the controller up for reversibility) costs almost nothing
and should stand.

**3. A connection is CHEAPER than advertising** — connected idle is **0.020 mA below**
`ble_adv`. Not a measurement artefact: it is repeatable and 3σ clear. An advertising event
transmits on three channels; a connection event is one TX + one RX. So a connected watch is
in the cheapest radio state it has.

**4. Data transfer is free at this device's rates.**

- Status notifications — 19 B every 2 s — cost **+0.000 mA**. The radio already wakes every
  connection interval whether or not there is a payload; 19 bytes ride along inside a slot
  that was paid for anyway.
- Hammering it with **sustained reads at 9.7/s** (387 ATT reads in 40 s, ~7.4 KB) costs
  **+0.038 mA** over connected idle. That is a saturating read loop, far past anything the
  product does, and it is still 1/9th of the gyro.

**Conclusion: BLE as scoped is essentially free.** The whole feature — advertiser, live
connection, and notification traffic — lives inside ~0.11 mA against a 2.6 mA floor that is
still ~80% unattributed (§3.9). There is nothing to optimise here; the display and the
idle floor remain the only things that matter.

### Caveats on §3.10

- **The connection interval was chosen by BlueZ, not by us.** The firmware never calls
  `bt_conn_le_param_update()`, so the central sets it. The observed 9.7 reads/s implies
  ~100 ms per ATT transaction, i.e. a ~100 ms interval. A phone that negotiates a much
  faster interval would raise the connected-state figures; one that negotiates a slow
  interval would lower them. **Findings 3 and 4 hold at ~100 ms and are not proven
  outside it.** If BLE ever runs connected for long periods, requesting an explicit
  connection interval is the lever, not the advertising interval.
- Notifications were confirmed live during the measurement (6 received in 12 s, exactly
  the 2 s `STATUS_NOTIFY_INTERVAL`), so "+0.000 mA" means *sent and free*, not *never
  sent*. The payload reads all-zero in this image because `ble_publish_status()` is only
  called from `sensor_update_thread`, which this harness does not run — uptime is the one
  live field and it tracked correctly.
- Measured at 3.84 V, not the ~3.29 V of the tables above. Per §3.5 the floor moves with
  supply voltage; the deltas are what transfer.

## 4. Open questions

1. **What owns the ~2.1 mA floor.** Leading hypothesis (**unverified**): **TPS63900 converter
   overhead at light load**, e.g. running in forced-PWM rather than power-save. It is the only
   thing that fits both a floor independent of all MCU activity *and* power rising 28% with
   input voltage (switching loss scales with Vin; a constant-power load would not).
   **Next steps:** check the TPS63900 MODE/PS pin wiring in the schematic, and measure current
   on the **VCC side** rather than VBAT to separate converter loss from actual load.
2. **Voltage-axis calibration.** PS readback runs ~0.13–0.15 V high and the DMM is occupied by
   the shunt. One DMM reading on the rail settles it — and with it, whether brownout is at
   2.85 or 3.02 V.
3. **Which BOOST_SEL level is 2.7 V vs 3.0 V** is *inferred from current*, not measured. A
   direct VCC reading in each mode would confirm it.
   **Partial answer 2026-08-26:** Anders measured **VCC = 2.7 V** on SN3 with a DMM while
   `power_save_active` read **false** in RAM — i.e. firmware believed it was driving the
   *normal, higher* rail. The MCP23008's buttons were dead at the same time, so the working
   theory is that the expander lost its configuration and GP6/BOOST_SEL was **floating**.
   That makes 2.7 V a reading of the *floating* mode-select, not of either driven level, so
   it does not close this question — but it does mean a floating GP6 lands on 2.7 V, which
   is worth knowing on its own. See §5.

4. **Does screen CONTENT cost power?** (raised 2026-08-26, unmeasured)

   §3.1 measured the panel at backlight steps but always showing the same thing. Nobody has
   varied what is *on* the screen. The 2026-08-26 sunlight rework flipped every menu and leaf
   screen from light-on-dark to **black-on-light**, and both the commit message and the design
   review assert that "unlike an OLED, a white ground costs no extra power here, because the
   backlight is on regardless." **That claim is reasoning, not a measurement**, and it is now
   load-bearing for a UI decision — if it is wrong, the new theme costs battery on every menu
   screen, and the correct response would be to keep the dark theme indoors and switch to the
   light one only in a sun mode.

   What to measure, panel on, backlight fixed at 50%, dwell >= 30 s per state (`DWELL_MS`'s
   8 s does not settle — see §2):

   | State | Why |
   |---|---|
   | Full black | Floor for content-dependent draw |
   | Full white | The theoretical worst case |
   | Real clock face (dark ground) | What the watch actually shows most of the time |
   | Real menu screen (light ground) | What the new theme actually shows |
   | Full white at 0% and 100% backlight | Whether content and backlight interact |

   **Expected result: a difference well under 0.1 mA** — this is a transmissive TFT with an
   LED backlight, so pixel content changes only the LC switching and the panel's own drive,
   not the light source. The measurement is worth taking *because* the expected answer is
   "no difference": that is exactly the kind of assumption that silently becomes wrong, and
   §3.1 already showed the panel logic alone (2.1 mA) dwarfs the entire backlight range
   (2.3 mA), so the panel side is not obviously negligible.

   Anything above ~0.1 mA between black and white means content-dependent draw is real and
   the light theme needs revisiting. Note this is also cheap to fold into an existing sweep —
   it needs no new firmware image, just a UI state to park in.
5. **SN1 has the wrong NAND** (1.8 V `MT29F2G01ABBGD`), so NVS never initialises and the
   `nvs_*` states are marked `invalid` in `schedule.json`. NVS write power is unmeasured.

## 5. Firmware follow-ups this exposed

- `power_save_enable()` has an **inverted sense** vs the hardware — fix before it gets a caller.
- **Guard the BOOST_SEL brownout latch** before anything is allowed to drive GP6 high.
- **`DWELL_MS` (8 s) is too short** — no state settles within it.
- **The gyro is always on and costs 0.34 mA.** Decide whether the logged dataset needs it.
- Low-power mode (`src/power/low_power.[ch]`) was built on these findings: display levers only,
  deliberately *not* BOOST_SEL. See its header for the reasoning.
