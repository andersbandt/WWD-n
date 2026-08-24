# Power Profiling

Characterizes current/power draw of individual subsystems (display backlight, battery-divider
FET, IMU at various ODRs, NVS writes, DC/DC vs LDO) via a dedicated firmware build that runs a
fixed, scripted sequence of states — see `src/power_profile/power_profile_main.c` for the full
state list and `schedule.json` for the timing (must stay in sync with the firmware — if you
change one, change the other).

**Board must be powered via the bench PS (not direct USB)** for a clean measurement — the
harness doesn't call `usb_enable()`, so USB isn't available as a console anyway. All logging
goes out over SEGGER RTT only, and is purely an optional audit trail: the analysis relies on
elapsed time against the known fixed schedule, not on anything read live from the device.

## 1. Build

```bash
cmake -B build_power_profile -DBOARD=nrf52833_ders/nrf52833 \
  -DBOARD_ROOT=/home/anders/Documents/NCS/WWD-n -DPOWER_PROFILE_BUILD=ON -GNinja
cmake --build build_power_profile
```

## 2. Wire up the bench

- Board VDDS ← PS channel 1 (3.3V) — **not** direct USB.
- DMM in parallel at the rail (voltage mode) — per `feedback_lab_ps_voltage` memory, the PS's
  own voltage reading runs high, so the DMM is the accurate voltage source.
- PS's own current telemetry (`Imeas`) is the current source.

## 3. Run

1. Flash: `nrfjprog --program build_power_profile/zephyr/zephyr.hex --sectorerase --verify -r`
   (`-r` resets and starts it immediately — this is t=0 for the schedule).
2. As close to that reset as practically possible, start the logger:
   ```python
   mcp__lab__log_start(use_dmm=True, use_ps=True, interval_s=0.2,
                        math_columns=["Power_mW = DMM_Meas1 * PS_Imeas1 * 1000"])
   ```
3. Wait for the sequence to finish — `len(schedule["states"]) * schedule["dwell_s"]` seconds
   (21 states × 8s = 168s as currently defined) plus a few seconds margin for the firmware's
   own 2s settle delay before its first state.
4. `mcp__lab__log_stop()` — note the returned CSV path.

## 4. Analyze

```bash
python3 analyze_power_profile.py <path-to-csv>
```

If the logger started noticeably after the actual reset (check the RTT log's first "BEGIN t=0"
timestamp against when you called `log_start`, if you captured RTT), pass `--t0-offset SECONDS`
to shift the schedule to compensate.

**Before trusting the output**: `analyze_power_profile.py` was written without ever having seen
a real `log_start` CSV — the column names it expects (`Time`, `DMM_Meas1`, `PS_Imeas1`,
`Power_mW`) are inferred from the tool's docstring, not verified against real output. Check the
CSV header on the first real run and adjust the `*_COL` constants at the top of the script if
they don't match.

## Known limitations / things to sanity-check on the first real run

- **PS current-sensing resolution** is likely only accurate to ~mA. Several target deltas here
  (battery-divider ~165µA static draw, low-ODR IMU) may sit below that noise floor — if so,
  those specific states may need the DMM switched to series/current mode instead (losing
  accurate voltage for just those states).
- **IMU "idle" isn't a real state in the current driver** — there's no stop/low-power API, so
  the state list is an ODR sweep (25-800 Hz) rather than including a true "not streaming"
  baseline. The lowest ODR point is the closest available reference.
- The `nvs_writing` and `combo_realistic` states self-time their own dwell (the write loop
  itself takes the full `DWELL_MS`) rather than being followed by a separate sleep — verify in
  the RTT log that their actual duration matches the schedule if timing ever looks off.

## Files here

- `schedule.json` — state name + elapsed-time start/end (seconds), hand-synced with the
  firmware's state table.
- `analyze_power_profile.py` — slices a `log_start` CSV by the schedule, prints per-state
  mean/std power/voltage/current and delta-from-baseline.

---

## Corrected method (from the first real runs, 2026-08-24)

The sections above were written before this harness had ever run. What actually works:

### Measure with USB unplugged — mandatory, not a preference
With USB attached the battery-port supply reads **0 A while the device runs**: VBUS back-feeds
VBAT through a hardware defect. Any USB-attached "measurement" characterises the defect, not
the firmware.

### Use PS channel 2, and a shunt rather than PS telemetry
The board is fed from **PS ch2** (`log_start(ps_channel=2)`), not ch1 as assumed above. PS
current telemetry quantises to **1 mA**, which buries the battery-divider (~110 µA), IMU-ODR
and DC/DC deltas — every `±std` reads 0.00 because it is quantisation, not stability.

The working rig is a **1.6 Ω shunt inline** with the XDM1041 in **mV mode, low sample speed**
(not the DMM in ammeter mode). Resolution ~10 µA, burden only ~11 mV at 7 mA:

```python
log_start(use_dmm=True, use_ps=True, ps_channel=2, interval_s=0.2,
          math_columns=["I_A = DMM_Meas1 / 1.6",
                        "V_board = PS_Vmeas1 - DMM_Meas1"])
```
```bash
python3 analyze_power_profile.py <csv> --t0-offset <fitted> \
    --current-col I_A --voltage-col V_board --settle-s 5.0 --edge-trim-s 0.5
```

### Fit t0 from the trace every run
Power-on to schedule t=0 is ~5 s of Zephyr init plus the firmware's 2 s settle, and it varies
run to run (9 s, then 8 s). Align on the **backlight staircase**; the confirmation you got it
right is that every state then reports the same sample count.

### The 8 s dwell does not settle
Every state ramps for ~6 s. Whole-window means carry ±1–3 mW; settled tails are ±0.00. Hence
`--settle-s`. If the harness is revised, raise `DWELL_MS`.

### Sweep supply voltage DOWNWARD, always
Sweeping up from below the brownout point silently corrupts every later point: the board
latches off and the "readings" are leakage (~0.1–0.19 mA, rising with V like a resistor).
Bin-and-average hides this entirely. Sweep down from a known-good voltage, and verify from the
raw time series that the board was actually running at each point.

### t=0 is a PS power-cycle, not `nrfjprog -r`
An attached J-Link keeps the debug domain powered and can block low-power states. Flash,
detach, then `ps_output` on. (Runs so far still had the probe attached, so the ~2.67 mA idle
floor is an upper bound.)

### Known open caveats
- PS voltage readback runs **~0.13–0.15 V above commanded**, and with the DMM on the shunt
  there is no independent rail measurement — the absolute voltage axis carries that error.
- `nvs_*` states are meaningless on SN1 (wrong 1.8 V NAND) and are marked `invalid` in
  `schedule.json`.
- IMU ODR shows **no** current dependence at 10 µA resolution, and the IMU states draw *less*
  than idle. Unexplained; backburnered.
