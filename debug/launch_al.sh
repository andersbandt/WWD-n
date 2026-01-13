#!/usr/bin/env bash
set -e

ELF=build/zephyr/zephyr.elf
DEVICE=nRF52832_xxAA
IFACE=SWD
SPEED=4000
PORT=2331

echo "[1] Starting J-Link GDB Server..."
JLinkGDBServer \
    -device $DEVICE \
    -if $IFACE \
    -speed $SPEED \
    -port $PORT \
    -singlerun \
    -nogui \
    -silent &

GDBSERVER_PID=$!

# Give server time to start
sleep 1

echo "[2] Launching GDB..."
arm-none-eabi-gdb $ELF \
    -ex "target remote localhost:$PORT" \
    -ex "monitor reset halt" \
    -ex "load" \
    -ex "break main" \
    -ex "continue"

# Cleanup when GDB exits
kill $GDBSERVER_PID
