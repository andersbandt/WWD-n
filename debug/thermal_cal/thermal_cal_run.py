#!/usr/bin/env python3
"""
Thermal calibration run for the WWD-n die temperature sensors.

WHAT THIS IS FOR
----------------
Two questions, one run:

  1. CALIBRATION — what are the real offset, gain and linearity of the IMU
     (ICM-42670) and SoC (nRF52833) temperature sensors, against a traceable
     K-type thermocouple? Today the firmware uses the ICM's NOMINAL 128 LSB/degC
     and a nominal 25 degC zero. The part is specced 125 / 126.9 / 129, so up to
     ~3% of unchecked gain error, which over a 5..50 degC sweep is +/-1.4 degC
     masquerading as offset. The nRF is specced +/-5 degC absolute.

  2. SELF-HEATING — how much of each die reading is the device warming itself?
     This CANNOT be answered by a temperature sweep alone: with the board
     powered the whole time, sensor offset and self-heating rise are perfectly
     degenerate, both being a constant added to a constant ambient. The only
     way to separate them is to CHANGE THE POWER, which is what the power
     cycling in here is for.

     Each measurement cycle parks the board UNPOWERED until it has equilibrated
     with the chamber, then powers it on and captures the warm-up transient:

         T(t) = T_inf - (T_inf - T_0) * exp(-t / tau)

     T_0 (the first reading after boot) is ambient with zero self-heating, and
     is what gets compared against the thermocouple for question 1.
     T_inf - T_0 is the self-heating rise for that operating state, which
     answers question 2.

     Measured on SN3 2026-08-28 from a cold-start boot in the log:
     tau = 180 s, rise = 3.97 degC, rms residual 0.19 degC on a single-exponential
     fit — so the device behaves as a clean single thermal lump and this model
     is the right one.

HOW THE TEMPERATURE IS SET
--------------------------
There is no cooling below room temperature, so the run has two phases:

  Phase A (sub-room, UNCONTROLLED): the oven, with the board inside, cold-soaks
     in a fridge. It is then taken OUT and allowed to drift up to room
     temperature in open air while this script logs. Measurement cycles are
     triggered by the thermocouple crossing each target on the way up.

     (The soak happens with the door shut and nothing logging: a fridge is a
     metal box and the ESP8266's WiFi will not survive it. Nothing is lost —
     the cold soak only has to make the board cold, not be measured.)

  Phase B (above room, CONTROLLED): the reflow controller's `hold` command
     parks the oven at each setpoint in closed loop. Its firmware rejects
     setpoints below 30 degC, which is exactly why Phase A exists.

WIRING / PRECONDITIONS  (the script checks what it can)
-------------------------------------------------------
  * Board powered ONLY from SPD3303X channel 1 at 3.3 V. Set the voltage on the
    supply beforehand; this script never writes a voltage, it only toggles the
    output. That way a bug in here cannot put the wrong rail on the board.
  * USB TO THE BOARD MUST BE DISCONNECTED (relay ch3 / MICRO-USB open). USB
    back-feeds the 3.3 V rail on this hardware, so with USB attached the
    "power cycles" would not actually cycle anything and the whole run would be
    silently worthless. The preflight check refuses to start if the board is
    still enumerating.
  * Therefore nothing is logged from the device live. It logs to its own NAND
    (RECORD_TEMPERATURE + RECORD_SOC_TEMP, paired, every 10 s) and you dump it
    over USB afterwards.

PAIRING THE TWO DATA SETS
-------------------------
Each measurement cycle is exactly one power cycle, so it is exactly one boot.
dump_decoder.py already tracks a `boot` column (it detects the tick count
restarting), so cycle N in cycles.json pairs with boot segment N in the dump.
cycles.json records the thermocouple reading at each power-on, which is the
ambient truth for that boot's T_0.

USAGE
-----
    python3 thermal_cal_run.py --out data/run1            # full run
    python3 thermal_cal_run.py --out data/run1 --phase b  # heated part only
    python3 thermal_cal_run.py --dry-run                  # preflight only

Ctrl-C at any point is safe: heaters are aborted and the supply output is
turned off in a finally block.
"""

import argparse
import base64
import csv
import glob
import json
import os
import socket
import struct
import sys
import time
from datetime import datetime

try:
    import requests
except ImportError:
    sys.exit("need `requests` (pip install requests)")

try:
    import pyvisa
except ImportError:
    sys.exit("need `pyvisa` (pip install pyvisa pyvisa-py)")


# ---------------------------------------------------------------------------
# Configuration
# ---------------------------------------------------------------------------

# WIFI_HOSTNAME in the controller's config.h is "reflow", but whether that
# actually resolves depends on the router handing out DHCP hostnames. mDNS
# (.local) is the usual fallback; an IP always works. Tried in order.
REFLOW_HOSTS = ["reflow", "reflow.local"]
REFLOW_WS_PATH = "/ws"

PS_CHANNEL = 1                # config/master.ini [Target] ps_channel
EXPECTED_VBOARD = 3.3         # only checked, never written
VBOARD_TOLERANCE = 0.25

TAU_S = 180.0                 # measured on SN3, 2026-08-28

# Phase A: thermocouple targets on the way up from fridge to room, in degC.
# Spaced ~3 degC: close enough to resolve curvature (the whole point of a
# linearity check), far enough apart that a cycle finishes before the next one
# is due. Trim the low end to whatever your fridge actually reaches.
PHASE_A_TARGETS = [8.0, 11.0, 14.0, 17.0, 20.0, 23.0]

# Phase B: closed-loop setpoints. 30 is the controller's floor for `hold`.
PHASE_B_SETPOINTS = [30.0, 35.0, 40.0, 45.0, 50.0]

# Hard ceiling enforced here regardless of what is asked for. The oven can do
# reflow temperatures; nothing in this experiment justifies going near them,
# and a typo should not cook the board.
MAX_SETPOINT_C = 55.0

# Cycle timings, seconds. Phase A is deliberately quicker: ambient is drifting
# the whole time, so a long cycle smears the point it is supposed to measure.
# Phase B has all the time in the world, so it captures the full transient.
PHASE_A_UNPOWERED_S = 120     # 0.7 tau — enough to shed most of the last rise
PHASE_A_POWERED_S = 240       # 1.3 tau — enough for T_0 + the start of the curve
PHASE_B_UNPOWERED_S = 600     # 3.3 tau — fully equilibrated with the chamber
PHASE_B_POWERED_S = 1200      # 6.7 tau — full transient out to T_inf

# Phase B settle criteria before a cycle starts.
SETTLE_TOL_C = 0.5
SETTLE_HOLD_S = 180
SETTLE_TIMEOUT_S = 2400

POLL_S = 1.0                  # thermocouple/PS logging period


# ---------------------------------------------------------------------------
# Minimal WebSocket text-frame sender
#
# The reflow controller takes `hold`/`abort` ONLY over its /ws WebSocket
# (webui.cpp handleCommand); /status is HTTP and read-only. Rather than add a
# websocket-client dependency for what amounts to two string sends, this does
# the handshake and one masked text frame by hand. We never read frames back —
# telemetry comes from HTTP /status — which is what keeps this short enough to
# be worth hand-rolling.
# ---------------------------------------------------------------------------

class WSCommandError(RuntimeError):
    pass


def ws_send_command(host, payload, path=REFLOW_WS_PATH, port=80, timeout=5.0):
    key = base64.b64encode(os.urandom(16)).decode()
    req = (
        f"GET {path} HTTP/1.1\r\n"
        f"Host: {host}\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        f"Sec-WebSocket-Key: {key}\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "\r\n"
    )

    with socket.create_connection((host, port), timeout=timeout) as sock:
        sock.settimeout(timeout)
        sock.sendall(req.encode())

        resp = b""
        while b"\r\n\r\n" not in resp:
            chunk = sock.recv(1024)
            if not chunk:
                raise WSCommandError("connection closed during handshake")
            resp += chunk
            if len(resp) > 8192:
                raise WSCommandError("handshake response too large")

        status = resp.split(b"\r\n", 1)[0].decode(errors="replace")
        if "101" not in status:
            raise WSCommandError(f"upgrade refused: {status}")

        data = payload.encode()
        if len(data) > 125:
            raise WSCommandError("command longer than a 125-byte frame")

        mask = os.urandom(4)
        masked = bytes(b ^ mask[i % 4] for i, b in enumerate(data))
        # FIN + opcode 0x1 (text); client frames must set the mask bit.
        frame = struct.pack("!BB", 0x81, 0x80 | len(data)) + mask + masked
        sock.sendall(frame)

        # Give the ESP8266 a moment to act on it before the socket closes.
        # ESPAsyncWebServer drops the client's queued frames on an abrupt
        # close, and this has to land.
        time.sleep(0.35)


# ---------------------------------------------------------------------------
# Reflow oven
# ---------------------------------------------------------------------------

class Reflow:
    def __init__(self, host=None):
        candidates = [host] if host else list(REFLOW_HOSTS)
        errors = []
        for cand in candidates:
            try:
                r = requests.get(f"http://{cand}/status", timeout=4)
                r.raise_for_status()
                r.json()
                self.host = cand
                self.url = f"http://{cand}/status"
                return
            except Exception as exc:
                errors.append(f"{cand}: {type(exc).__name__}")
        raise RuntimeError(
            "reflow controller unreachable (" + "; ".join(errors) + "). "
            "Is the oven powered and on WiFi? Pass --reflow-host <ip> if the "
            "name does not resolve."
        )

    def status(self):
        """Returns the controller's telemetry dict, or raises."""
        r = requests.get(self.url, timeout=4)
        r.raise_for_status()
        return r.json()

    def temp(self):
        """Thermocouple degC. Raises on a MAX31856 fault rather than returning
        the firmware's -999 sentinel, so a detached probe stops the run instead
        of quietly poisoning a calibration data set."""
        s = self.status()
        t = float(s.get("t1", -999.0))
        fault = int(s.get("fault", 0) or 0)
        if t <= -900.0 or s.get("t1_fault"):
            raise RuntimeError(f"thermocouple fault (raw MAX31856 fault 0x{fault:02x})")
        return t, s

    def hold(self, setpoint_c):
        if not (30.0 <= setpoint_c <= MAX_SETPOINT_C):
            raise ValueError(
                f"setpoint {setpoint_c} outside this script's [30, {MAX_SETPOINT_C}]"
            )
        ws_send_command(self.host, json.dumps({"cmd": "hold", "sp": setpoint_c}))

    def abort(self):
        ws_send_command(self.host, json.dumps({"cmd": "abort"}))


# ---------------------------------------------------------------------------
# Bench supply
# ---------------------------------------------------------------------------

class Supply:
    """SPD3303X output control. Deliberately toggle + measure only — the
    voltage setpoint is left exactly as the user configured it."""

    def __init__(self, channel=PS_CHANNEL, address=None):
        self.channel = channel
        self.rm = pyvisa.ResourceManager("@py")
        self.inst = None
        self.idn = ""

        candidates = [address] if address else list(self.rm.list_resources())
        for res in candidates:
            try:
                inst = self._open(res)
                idn = inst.query("*IDN?").strip()
            except Exception:
                continue
            if "SPD3303X" in idn.upper().replace(" ", ""):
                self.inst, self.idn = inst, idn
                break
            try:
                inst.close()
            except Exception:
                pass

        if self.inst is None:
            raise RuntimeError(
                "no SPD3303X found. It is often a TCPIP/USB instrument that the "
                "@py backend will not enumerate — pass --visa with its address, "
                "e.g. --visa TCPIP0::192.168.1.50::INSTR"
            )

    def _open(self, res):
        # Termination and timeout copied from EEequipment/SPD3303X/config.ini,
        # which is the known-good configuration for this instrument.
        inst = self.rm.open_resource(res)
        inst.timeout = 3000
        inst.write_termination = "\n"
        inst.read_termination = "\n"
        return inst

    def output(self, on):
        # Long-form SCPI, matching config.ini's output_on/output_off exactly.
        self.inst.write(f"OUTPut CH{self.channel},{'ON' if on else 'OFF'}")
        time.sleep(0.2)

    def measure(self):
        try:
            v = float(self.inst.query(f"MEASure:VOLTage? CH{self.channel}"))
            i = float(self.inst.query(f"MEASure:CURRent? CH{self.channel}"))
            return v, i * 1000.0
        except Exception:
            return float("nan"), float("nan")

    def close(self):
        try:
            self.inst.close()
        except Exception:
            pass


# ---------------------------------------------------------------------------
# Run
# ---------------------------------------------------------------------------

CSV_COLUMNS = [
    "iso_time", "unix_time", "phase", "cycle", "event",
    "tc_c", "oven_state", "oven_sp", "oven_duty",
    "board_power", "ps_v", "ps_i_ma",
]


class Run:
    def __init__(self, outdir, reflow, supply):
        os.makedirs(outdir, exist_ok=True)
        self.outdir = outdir
        self.reflow = reflow
        self.supply = supply
        self.cycle = 0
        self.phase = "-"
        self.board_power = False
        self.cycles = []

        stamp = datetime.now().strftime("%Y%m%d%H%M%S")
        self.csv_path = os.path.join(outdir, f"THERMAL_{stamp}.csv")
        self.json_path = os.path.join(outdir, f"THERMAL_{stamp}_cycles.json")
        self._fh = open(self.csv_path, "w", newline="")
        self._w = csv.DictWriter(self._fh, fieldnames=CSV_COLUMNS)
        self._w.writeheader()

    def log(self, event=""):
        """One sample: thermocouple + oven state + supply. Returns the TC."""
        tc, s = self.reflow.temp()
        v, i = self.supply.measure() if self.board_power else (0.0, 0.0)
        now = time.time()
        self._w.writerow({
            "iso_time": datetime.fromtimestamp(now).isoformat(timespec="milliseconds"),
            "unix_time": f"{now:.3f}",
            "phase": self.phase,
            "cycle": self.cycle,
            "event": event,
            "tc_c": f"{tc:.3f}",
            "oven_state": s.get("state", ""),
            "oven_sp": s.get("sp", ""),
            "oven_duty": s.get("duty", ""),
            "board_power": int(self.board_power),
            "ps_v": f"{v:.4f}",
            "ps_i_ma": f"{i:.3f}",
        })
        self._fh.flush()
        return tc

    def dwell(self, seconds, event=""):
        """Log at POLL_S for `seconds`, returning the last thermocouple value."""
        end = time.time() + seconds
        tc = None
        first = True
        while time.time() < end:
            tc = self.log(event if first else "")
            first = False
            remaining = end - time.time()
            if remaining > 0:
                time.sleep(min(POLL_S, remaining))
        return tc

    def measurement_cycle(self, phase, label):
        """One power cycle == one device boot == one dump segment."""
        self.cycle += 1
        self.phase = phase
        unpowered = PHASE_A_UNPOWERED_S if phase == "A" else PHASE_B_UNPOWERED_S
        powered = PHASE_A_POWERED_S if phase == "A" else PHASE_B_POWERED_S

        print(f"\n=== cycle {self.cycle} ({phase}, {label}) ===")

        print(f"  board OFF, equilibrating {unpowered}s ...")
        self.supply.output(False)
        self.board_power = False
        self.dwell(unpowered, event="unpowered_start")

        # The instant of power-on is the whole experiment: the device's first
        # temperature record after this is ambient with no self-heating, and
        # tc_at_power_on is the truth it gets compared against.
        tc_on = self.log(event="power_on")
        self.supply.output(True)
        self.board_power = True
        t_on = time.time()
        print(f"  board ON at TC={tc_on:.2f} C, capturing {powered}s ...")
        self.dwell(powered, event="powered_start")

        tc_off = self.log(event="power_off")
        self.supply.output(False)
        self.board_power = False

        self.cycles.append({
            "cycle": self.cycle,
            "phase": phase,
            "label": label,
            "power_on_unix": t_on,
            "power_on_iso": datetime.fromtimestamp(t_on).isoformat(timespec="seconds"),
            "tc_at_power_on": round(tc_on, 3),
            "tc_at_power_off": round(tc_off, 3),
            "powered_s": powered,
        })
        with open(self.json_path, "w") as f:
            json.dump({"tau_s": TAU_S, "cycles": self.cycles}, f, indent=2)
        print(f"  done (TC {tc_on:.2f} -> {tc_off:.2f} C)")

    def close(self):
        try:
            self._fh.close()
        except Exception:
            pass


# ---------------------------------------------------------------------------

def preflight(reflow, supply, allow_usb=False):
    print("preflight:")

    tc, s = reflow.temp()
    print(f"  reflow '{reflow.host}' reachable, TC = {tc:.2f} C, state = {s.get('state')}")

    # Anything other than idle means the oven is mid-profile. Refuse rather
    # than fight another controller for the heaters.
    if s.get("state") not in ("IDLE", "DONE", "ERROR", "HOLD"):
        raise RuntimeError(f"oven is busy (state={s.get('state')}) — abort it first")

    supply.output(False)
    time.sleep(0.5)
    v, i = supply.measure()
    print(f"  supply: {supply.idn}")
    print(f"  CH{supply.channel} with output OFF reads {v:.3f} V, {i:.1f} mA")

    # USB back-feeds the 3.3 V rail on this hardware, so with the cable in the
    # power cycles would not cycle anything. If the board still enumerates with
    # the supply off, that is exactly what is happening.
    ports = sorted(glob.glob("/dev/ttyACM*"))
    if ports and not allow_usb:
        raise RuntimeError(
            f"board still enumerating on USB ({', '.join(ports)}) with the supply "
            "off — USB is back-feeding the rail. Open relay ch3 (MICRO-USB) / "
            "unplug the cable, or pass --allow-usb if those ports are something else."
        )
    print(f"  USB: {'no ACM ports (good)' if not ports else 'present: ' + ','.join(ports)}")

    supply.output(True)
    time.sleep(1.0)
    v, i = supply.measure()
    supply.output(False)
    print(f"  CH{supply.channel} with output ON reads {v:.3f} V, {i:.1f} mA")
    if abs(v - EXPECTED_VBOARD) > VBOARD_TOLERANCE:
        raise RuntimeError(
            f"CH{supply.channel} is {v:.3f} V, expected ~{EXPECTED_VBOARD} V. "
            "Set the supply voltage yourself — this script will not write it."
        )
    print("  preflight OK")


def wait_for_settle(run, setpoint):
    """Wait until the thermocouple sits within tolerance of the setpoint for
    SETTLE_HOLD_S continuously. The oven overshoots and rings on the way in, so
    a single in-band sample means nothing."""
    print(f"  settling at {setpoint:.1f} C (+/-{SETTLE_TOL_C}) for {SETTLE_HOLD_S}s ...")
    deadline = time.time() + SETTLE_TIMEOUT_S
    in_band_since = None
    first = True
    while time.time() < deadline:
        # Stamped once, like every other event, so the column stays a marker of
        # phase boundaries rather than a per-row label.
        tc = run.log(event="settling" if first else "")
        first = False
        if abs(tc - setpoint) <= SETTLE_TOL_C:
            in_band_since = in_band_since or time.time()
            if time.time() - in_band_since >= SETTLE_HOLD_S:
                print(f"  settled at {tc:.2f} C")
                return True
        else:
            in_band_since = None
        time.sleep(POLL_S)
    raise RuntimeError(f"oven never settled at {setpoint} C within {SETTLE_TIMEOUT_S}s")


def phase_a(run):
    print("\n########## PHASE A — fridge warm-up (uncontrolled) ##########")
    print("Take the oven out of the fridge NOW, board inside, door shut,")
    print("supply leads connected, USB disconnected.")
    input("Press Enter once it is out and closed up... ")

    run.phase = "A"
    remaining = list(PHASE_A_TARGETS)
    start_tc = run.log(event="phase_a_start")
    print(f"  starting at TC = {start_tc:.2f} C")

    # Targets already passed are unreachable on a monotonic warm-up.
    skipped = [t for t in remaining if t <= start_tc]
    if skipped:
        print(f"  skipping already-passed targets: {skipped}")
        remaining = [t for t in remaining if t > start_tc]

    while remaining:
        target = remaining[0]
        tc = run.log()
        if tc >= target:
            remaining.pop(0)
            run.measurement_cycle("A", f"{target:.0f}C_rising")
        else:
            time.sleep(POLL_S)
    print("\nPhase A complete.")


def phase_b(run, reflow):
    print("\n########## PHASE B — heated setpoints (closed loop) ##########")
    run.phase = "B"
    for sp in PHASE_B_SETPOINTS:
        if sp > MAX_SETPOINT_C:
            print(f"  refusing setpoint {sp} > {MAX_SETPOINT_C}")
            continue
        print(f"\n-- hold {sp:.1f} C --")
        reflow.hold(sp)
        wait_for_settle(run, sp)
        run.measurement_cycle("B", f"{sp:.0f}C_hold")
    reflow.abort()
    print("\nPhase B complete, heaters aborted.")


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", default="debug/thermal_cal/data",
                    help="output directory for the CSV + cycles.json")
    ap.add_argument("--phase", choices=["a", "b", "ab"], default="ab",
                    help="which phases to run (default both)")
    ap.add_argument("--dry-run", action="store_true",
                    help="preflight only, then exit")
    ap.add_argument("--reflow-host", default=None,
                    help="hostname or IP of the reflow controller "
                         f"(default: try {', '.join(REFLOW_HOSTS)})")
    ap.add_argument("--visa", default=None,
                    help="explicit VISA address for the SPD3303X")
    ap.add_argument("--allow-usb", action="store_true",
                    help="skip the USB back-feed check (only if those ACM ports "
                         "belong to something other than the board)")
    args = ap.parse_args()

    reflow = Reflow(args.reflow_host)
    supply = Supply(address=args.visa)
    run = None
    try:
        preflight(reflow, supply, allow_usb=args.allow_usb)
        if args.dry_run:
            print("dry run — nothing else to do")
            return 0

        run = Run(args.out, reflow, supply)
        print(f"\nlogging to {run.csv_path}")

        if "a" in args.phase:
            phase_a(run)
        if "b" in args.phase:
            phase_b(run, reflow)

        print("\n=========================================================")
        print(f"CSV     : {run.csv_path}")
        print(f"cycles  : {run.json_path}")
        print(f"{run.cycle} measurement cycles == {run.cycle} device boots.")
        print("\nNext:")
        print("  1. Reconnect USB (relay ch3) and dump the device log.")
        print("  2. Decode it — cycle N in cycles.json pairs with boot segment N.")
        print("  3. Per boot: first TEMPERATURE/SOC_TEMP record = T_0 (ambient,")
        print("     no self-heating) -> compare against tc_at_power_on for the")
        print("     calibration curve. Fit the boot's warm-up for tau and")
        print("     T_inf - T_0 = the self-heating rise at that ambient.")
        return 0

    except KeyboardInterrupt:
        print("\ninterrupted")
        return 130
    except Exception as exc:
        print(f"\nERROR: {exc}")
        return 1
    finally:
        # Heaters off and rail down, whatever happened above.
        try:
            reflow.abort()
            print("heaters aborted")
        except Exception as exc:
            print(f"WARNING: could not abort heaters: {exc} — CHECK THE OVEN")
        try:
            supply.output(False)
            print("supply output off")
        except Exception as exc:
            print(f"WARNING: could not turn the supply off: {exc}")
        if run is not None:
            run.close()
        supply.close()


if __name__ == "__main__":
    sys.exit(main())
