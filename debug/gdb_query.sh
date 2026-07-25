#!/usr/bin/env bash
#
# gdb_query.sh — Run GDB commands against a live nRF52832 target and capture output.
#
# Usage:
#   ./gdb_query.sh "p my_var" "x/4wx 0x40004000" ...
#
#   Each argument becomes one GDB -ex command, executed in order after
#   connecting and halting the target. Output is written to:
#       debug/gdb_query_output.txt
#   and also printed to stdout.
#
# Examples:
#   ./gdb_query.sh "p rtc_seconds" "p rtc_tick_hz"
#   ./gdb_query.sh "x/16wx 0x40004000"          # dump SPIM1 registers
#   ./gdb_query.sh "info threads" "bt"
#   ./gdb_query.sh "p/x *(NRF_SPIM_Type*)0x40004000"

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

ELF="${ELF:-$REPO_ROOT/build_n33/zephyr/zephyr.elf}"
DEVICE="${DEVICE:-nRF52833_xxAA}"
IFACE=SWD
SPEED=4000
PORT=2331
OUTPUT="$SCRIPT_DIR/gdb_query_output.txt"

# ── Sanity checks ────────────────────────────────────────────────────────────

if [ $# -eq 0 ]; then
    echo "Usage: $0 \"<gdb command>\" [\"<gdb command>\" ...]" >&2
    exit 1
fi

if [ ! -f "$ELF" ]; then
    echo "ERROR: ELF not found at $ELF — run a build first." >&2
    exit 1
fi

# ── Start J-Link GDB server ───────────────────────────────────────────────────

# Always kill any leftover server first (-singlerun servers linger briefly on exit)
EXISTING_PID=$(lsof -iTCP:$PORT -sTCP:LISTEN -t 2>/dev/null || true)
if [ -n "$EXISTING_PID" ]; then
    echo "[gdb_query] Killing stale server on :$PORT (PID $EXISTING_PID)..."
    kill "$EXISTING_PID" 2>/dev/null || true
    sleep 0.5
fi

echo "[gdb_query] Starting J-Link GDB server on :$PORT ..."
JLinkGDBServer \
    -device $DEVICE \
    -if $IFACE \
    -speed $SPEED \
    -port $PORT \
    -singlerun \
    -nogui \
    -silent &
GDBSERVER_PID=$!

cleanup() {
    echo "[gdb_query] Stopping J-Link GDB server (PID $GDBSERVER_PID)..."
    kill "$GDBSERVER_PID" 2>/dev/null || true
}
trap cleanup EXIT

# Wait until server accepts connections (up to 5 s)
for i in $(seq 1 50); do
    if lsof -iTCP:$PORT -sTCP:LISTEN -t >/dev/null 2>&1; then
        break
    fi
    sleep 0.1
done

if ! lsof -iTCP:$PORT -sTCP:LISTEN -t >/dev/null 2>&1; then
    echo "ERROR: J-Link GDB server did not start in time." >&2
    exit 1
fi
echo "[gdb_query] Server ready."

# ── Build -ex argument list ───────────────────────────────────────────────────

EX_ARGS=()
for cmd in "$@"; do
    EX_ARGS+=(-ex "$cmd")
done

# ── Run GDB in batch mode ─────────────────────────────────────────────────────

echo "[gdb_query] Connecting and running $# command(s)..."
echo ""

arm-none-eabi-gdb "$ELF" \
    --batch \
    --quiet \
    -ex "set pagination off" \
    -ex "target remote localhost:$PORT" \
    -ex "monitor reset halt" \
    "${EX_ARGS[@]}" \
    -ex "detach" \
    2>&1 | tee "$OUTPUT"

echo ""
echo "[gdb_query] Output saved to: $OUTPUT"
