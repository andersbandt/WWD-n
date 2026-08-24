#!/usr/bin/env python3
"""
Slice a power-profiling CSV (from the lab MCP's log_start/log_stop) by the
known firmware state schedule (schedule.json) and print a per-state summary
(mean/std power, voltage, current), plus delta-from-baseline.

Usage:
    python3 analyze_power_profile.py <csv_path> [--t0-offset SECONDS]
                                      [--schedule schedule.json]

Assumes:
    - The CSV was started as close as practically possible to the firmware's
      boot/reset (t=0 in schedule.json's offsets). Any skew between "when I
      hit reset" and "when I called log_start" should be passed via
      --t0-offset (positive if logging started AFTER reset, i.e. shift the
      schedule earlier to compensate) — eyeball it from when the first
      state's expected effect (e.g. backlight ramp) shows up in the data.
    - CSV columns include a "Time" column and whatever was requested via
      log_start's math_columns, e.g. "Power_mW". Column names below
      (TIME_COL, VOLTAGE_COL, CURRENT_COL, POWER_COL) may need adjusting to
      match the actual CSV header once a real run exists — this was written
      before ever seeing a real log_start CSV, so verify column names first.
    - The Time column may be either an elapsed-seconds float or a timestamp
      string; this script tries both and uses elapsed-seconds-since-first-row
      either way.
"""

import argparse
import csv
import json
import statistics
import sys
from datetime import datetime
from pathlib import Path

# Adjust these if the real log_start CSV header differs.
TIME_COL = "Time"
# Verified against a real log_start CSV 2026-08-24. Header is:
#   Time,DMM_Meas1,PS_Vset1,PS_Vmeas1,PS_Imeas1,PS_Iset1,PS_Vset2,...,Power_mW
# NB the "1" suffix means "first logged channel", NOT PS channel 1 -- with
# log_start(ps_channel=2) the PS_*1 columns carry channel 2.
# Default to the PS rail voltage rather than DMM_Meas1: in the two-pass
# method the single DMM is needed in series for current on the low-current
# pass, so it is not measuring rail voltage. Override with --voltage-col.
VOLTAGE_COL = "PS_Vmeas1"
CURRENT_COL = "PS_Imeas1"
POWER_COL = "Power_mW"

TIME_FORMATS = [
    "%Y-%m-%d %H:%M:%S.%f",
    "%Y-%m-%d %H:%M:%S",
    "%H:%M:%S.%f",
    "%H:%M:%S",
]


def parse_time_value(raw, first_raw):
    """Return elapsed seconds since first_raw, trying numeric then datetime."""
    try:
        return float(raw) - float(first_raw)
    except ValueError:
        pass

    for fmt in TIME_FORMATS:
        try:
            t = datetime.strptime(raw, fmt)
            t0 = datetime.strptime(first_raw, fmt)
            return (t - t0).total_seconds()
        except ValueError:
            continue

    raise ValueError(
        f"Could not parse Time value {raw!r} as a number or known datetime "
        f"format — check TIME_FORMATS in this script against the real CSV."
    )


def load_rows(csv_path):
    with open(csv_path, newline="") as f:
        reader = csv.DictReader(f)
        rows = list(reader)

    if not rows:
        raise ValueError(f"{csv_path} has no data rows")

    for col in (TIME_COL, VOLTAGE_COL, CURRENT_COL):
        if col not in rows[0]:
            raise ValueError(
                f"Expected column {col!r} not found in CSV header "
                f"{list(rows[0].keys())} — adjust the *_COL constants at the "
                f"top of this script to match the real log_start output."
            )

    first_time = rows[0][TIME_COL]
    for row in rows:
        row["_elapsed_s"] = parse_time_value(row[TIME_COL], first_time)
        row["_voltage"] = float(row[VOLTAGE_COL])
        row["_current"] = float(row[CURRENT_COL])
        # Always recompute rather than trusting the logger's Power_mW column:
        # that was evaluated from PS_Vmeas1*PS_Imeas1 at capture time and is
        # wrong whenever --voltage-col/--current-col point somewhere else
        # (e.g. the DMM-in-series low-current pass).
        row["_power_mw"] = row["_voltage"] * row["_current"] * 1000.0

    return rows


def summarize(rows, start_s, end_s):
    window = [r for r in rows if start_s <= r["_elapsed_s"] < end_s]
    if not window:
        return None

    powers = [r["_power_mw"] for r in window]
    voltages = [r["_voltage"] for r in window]
    currents = [r["_current"] for r in window]

    return {
        "n": len(window),
        "power_mw_mean": statistics.mean(powers),
        "power_mw_std": statistics.pstdev(powers) if len(powers) > 1 else 0.0,
        "voltage_mean": statistics.mean(voltages),
        "current_ma_mean": statistics.mean(currents) * 1000.0,
    }


def main():
    global VOLTAGE_COL, CURRENT_COL
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("csv_path", type=Path)
    ap.add_argument("--schedule", type=Path,
                     default=Path(__file__).parent / "schedule.json")
    ap.add_argument("--t0-offset", type=float, default=0.0,
                     help="Seconds to shift the schedule by (positive if "
                          "logging started after reset)")
    ap.add_argument("--voltage-col", default=VOLTAGE_COL,
                     help=f"CSV column for voltage (default {VOLTAGE_COL}); "
                          "use DMM_Meas1 when the DMM is across the rail")
    ap.add_argument("--current-col", default=CURRENT_COL,
                     help=f"CSV column for current (default {CURRENT_COL}); "
                          "use DMM_Meas1 when the DMM is in series")
    ap.add_argument("--settle-s", type=float, default=None,
                     help="Override schedule settle_margin_s: seconds to "
                          "discard from the START of each state. Raise this "
                          "for the DMM/shunt pass -- the meter's slow sample "
                          "rate plus genuinely slow settling (IMU states ramp "
                          "for ~6s) contaminates the head of each window")
    ap.add_argument("--edge-trim-s", type=float, default=1.0,
                     help="Seconds to discard from the END of each state "
                          "window. log_start timestamps have only 1-second "
                          "resolution even at interval_s=0.2, so samples near "
                          "a boundary may belong to the neighbouring state "
                          "(default 1.0)")
    args = ap.parse_args()

    VOLTAGE_COL = args.voltage_col
    CURRENT_COL = args.current_col

    schedule = json.loads(args.schedule.read_text())
    rows = load_rows(args.csv_path)

    settle = (args.settle_s if args.settle_s is not None
              else schedule.get("settle_margin_s", 1.0))
    results = {}

    print(f"{'state':<20} {'n':>5} {'power_mW':>10} {'±std':>8} "
          f"{'V':>7} {'mA':>8}")
    print("-" * 65)

    baseline_power = None
    for st in schedule["states"]:
        start_s = st["start_s"] + args.t0_offset + settle
        end_s = st["end_s"] + args.t0_offset - args.edge_trim_s

        if st.get("invalid"):
            print(f"{st['name']:<20} {'SKIPPED — ' + st.get('invalid_reason', 'marked invalid'):>50}")
            results[st["name"]] = None
            continue

        summary = summarize(rows, start_s, end_s)
        results[st["name"]] = summary

        if summary is None:
            print(f"{st['name']:<20} {'(no data in this window)':>50}")
            continue

        if st["name"] in ("floor_dcdc_on",) and baseline_power is None:
            baseline_power = summary["power_mw_mean"]

        caveat = f"   <-- {st['caveat']}" if st.get("caveat") else ""
        print(f"{st['name']:<20} {summary['n']:>5} "
              f"{summary['power_mw_mean']:>10.2f} {summary['power_mw_std']:>8.2f} "
              f"{summary['voltage_mean']:>7.3f} {summary['current_ma_mean']:>8.2f}"
              f"{caveat}")

    if baseline_power is not None:
        print("\ndelta from floor_dcdc_on baseline:")
        for name, summary in results.items():
            if summary is None or name == "floor_dcdc_on":
                continue
            delta = summary["power_mw_mean"] - baseline_power
            print(f"  {name:<20} {delta:+8.2f} mW")


if __name__ == "__main__":
    sys.exit(main())
