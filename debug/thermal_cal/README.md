# Thermal calibration run

Calibrates the WWD-n die temperature sensors (ICM-42670 and nRF52833) against
the reflow oven's K-type thermocouple, **and** separates sensor offset from
self-heating in the same run.

Runner: `thermal_cal_run.py`.

## Why the power cycling

A temperature sweep alone cannot answer the self-heating question. With the
board powered throughout, sensor offset and self-heating rise are perfectly
degenerate — both are a constant added to a constant ambient, and no amount of
sweeping separates two constants that always appear summed.

Changing the *power* breaks the degeneracy. Each measurement cycle parks the
board unpowered until it has equilibrated with the chamber, then powers it on
and captures the warm-up:

```
T(t) = T_inf - (T_inf - T_0) * exp(-t / tau)
```

- **`T_0`** — first reading after boot, i.e. ambient with zero self-heating.
  Compared against the thermocouple, this is the calibration point.
- **`T_inf - T_0`** — the self-heating rise for that operating state.

Fitted from a real cold-start boot in the 2026-08-28 SN3 log:
**tau = 180 s, rise = 3.97 degC, rms residual 0.19 degC.** A residual that small on a
single exponential says the device is a clean single thermal lump, which is
what makes this model the right one. All the dwell times in the script are
written as multiples of that tau.

## Two phases, because there is no cooling

| Phase | Range | How | Trigger |
|---|---|---|---|
| **A** | fridge -> room | uncontrolled drift after the oven comes out of the fridge | thermocouple crossing each target in `PHASE_A_TARGETS` |
| **B** | 30 .. 50 degC | reflow controller `hold` in closed loop | settle at each `PHASE_B_SETPOINTS` entry |

The controller's firmware rejects `hold` setpoints below 30 degC, which is
exactly why Phase A is uncontrolled drift rather than a setpoint list.

Cold-soak in the fridge with nothing logging — a fridge is a metal box and the
ESP8266's WiFi will not survive it. Nothing is lost: the soak only has to make
the board cold, not be measured. Logging starts when it comes out.

## Before you run

1. **Board on SPD3303X CH1 at 3.3 V.** Set the voltage on the supply yourself.
   The script only toggles the output and never writes a voltage, so a bug in
   it cannot put the wrong rail on the board. Preflight refuses to start if
   CH1 is not within 0.25 V of 3.3.
2. **Disconnect USB from the board** (open relay ch3 / MICRO-USB). USB
   back-feeds the 3.3 V rail on this hardware, so with the cable in the "power
   cycles" would not cycle anything and the whole run would be silently
   worthless. Preflight aborts if the board still enumerates while the supply
   output is off.
3. **Consider erasing the NAND first** (`CMD_ERASE` over the protocol) so the
   dump contains only this experiment. The script does not do this — it is
   destructive and it is your data.
4. Oven powered and on WiFi. `python3 thermal_cal_run.py --dry-run` checks
   everything reachable without heating anything or cycling the board.

## Running

```bash
python3 debug/thermal_cal/thermal_cal_run.py --out debug/thermal_cal/data/run1
python3 debug/thermal_cal/thermal_cal_run.py --phase b   # heated part only
python3 debug/thermal_cal/thermal_cal_run.py --dry-run   # preflight only
```

Useful flags: `--reflow-host <ip>` if neither `reflow` nor `reflow.local`
resolves, `--visa <address>` if the `@py` backend does not enumerate the
supply (it often will not for USB-TMC or LAN instruments).

Ctrl-C is safe at any point — heaters are aborted and the supply output is
turned off in a `finally` block.

Rough duration: Phase A tracks the fridge warm-up (a couple of hours, set by
physics, not the script); Phase B is about 40 min per setpoint, so ~3.5 h for
five setpoints.

## Output, and how to pair it with the device

The script writes two files:

- `THERMAL_<stamp>.csv` — 1 Hz thermocouple, oven state/setpoint/duty, board
  power state, supply V and mA. Column `event` marks `power_on` / `power_off` /
  `settling` / phase boundaries.
- `THERMAL_<stamp>_cycles.json` — one entry per measurement cycle with the
  power-on wall-clock time and **`tc_at_power_on`**, the ambient truth for that
  cycle.

The device side needs no new firmware: it already logs `RECORD_TEMPERATURE` and
`RECORD_SOC_TEMP` paired sample-for-sample every 10 s to NAND. Dump it
afterwards over USB.

**Pairing rule: one measurement cycle = one power cycle = one boot.**
`dump_decoder.py` already tracks a `boot` column (it detects the tick count
restarting), so **cycle N in cycles.json is boot segment N in the dump.**

Then, per boot segment:

- first `TEMPERATURE` / `SOC_TEMP` record = `T_0`. Plot against
  `tc_at_power_on` across all cycles -> offset, gain and linearity for both
  sensors.
- fit that segment's warm-up -> `tau` and `T_inf - T_0`, the self-heating rise,
  and whether it varies with ambient.

## Known caveats

- **Condensation.** Fridge-cold to 50 degC takes a bare PCB through the dew
  point on the way up. Seal it with desiccant for the cold soak and open it
  only once it is above dew point, or start the soak at ~15 degC instead. Not
  worth killing a board over a calibration run.
- **Ramp lag in Phase A.** The die lags ambient by roughly `tau * dT/dt`. A
  fridge warm-up drifts around 0.1 degC/min, giving ~0.3 degC of lag — correct it
  with `T_true = T_measured + tau * dT/dt` using the CSV's own slope.
- **Thermocouple placement.** Touching the board/enclosure, not floating in
  air, or you are comparing the device's thermal mass against air temperature
  and calling the difference sensor error.
- **Phase A cycles are short on purpose** (2 min unpowered / 4 min powered).
  Ambient is drifting the whole time, so a long cycle smears the point it is
  meant to measure. Phase A is for the calibration curve; Phase B, with all the
  time in the world, is where the self-heating characterisation happens.
