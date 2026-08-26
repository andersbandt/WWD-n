#!/usr/bin/env python3
"""Drive the BLE connection phases for a power measurement.

The firmware (POWER_PROFILE_BLE image) holds the board advertising forever
after its three scripted states. A connection cannot be scripted from the
device side -- the central owns connect/subscribe/read -- so this walks the
phases from the host and prints wall-clock boundaries to slice the current log
against.

Run it from the wwd_gui_api repo root so `common.ble_client` imports.
"""
import sys, time
sys.path.insert(0, "/home/anders/Documents/GitHub/wwd_gui_api")

from common.ble_client import (_bus, find_device, connect, disconnect,
                               find_chrc, UUID_STATUS, decode_status)
import dbus
from gi.repository import GLib

CHRC = "org.bluez.GattCharacteristic1"

PHASES = []

def mark(name):
    t = time.time()
    PHASES.append((name, t))
    print(f"PHASE {name:20s} t_wall={t:.2f}  {time.strftime('%H:%M:%S')}", flush=True)

def hold(secs):
    time.sleep(secs)

def main():
    conn_idle_s = int(sys.argv[1]) if len(sys.argv) > 1 else 40
    notify_s    = int(sys.argv[2]) if len(sys.argv) > 2 else 60
    reads_s     = int(sys.argv[3]) if len(sys.argv) > 3 else 40
    post_s      = int(sys.argv[4]) if len(sys.argv) > 4 else 30

    bus = _bus()
    print("scanning for WWD-n ...", flush=True)
    dev = find_device(bus)
    if not dev:
        print("device not found -- is it advertising?"); return 1
    print("found:", dev, flush=True)

    mark("connect_begin")
    connect(bus, dev)
    # find_chrc already returns a bound dbus Interface, not a path.
    chrc = find_chrc(bus, dev, UUID_STATUS)
    # settle: let BlueZ finish discovery/param negotiation before timing starts
    hold(8)

    mark("conn_idle")
    hold(conn_idle_s)

    mark("conn_notify")
    chrc.StartNotify()
    hold(notify_s)
    chrc.StopNotify()

    mark("conn_reads")
    n = 0
    end = time.time() + reads_s
    while time.time() < end:
        try:
            chrc.ReadValue({})
            n += 1
        except Exception as e:
            print("read failed:", e); break
    print(f"  sustained reads: {n} in {reads_s}s = {n/reads_s:.1f}/s", flush=True)

    mark("disconnect")
    disconnect(bus, dev)
    hold(post_s)
    mark("end")

    print("\n--- phase boundaries (epoch seconds) ---")
    for name, t in PHASES:
        print(f"{name}\t{t:.2f}")
    return 0

if __name__ == "__main__":
    sys.exit(main())
