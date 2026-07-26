# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## On Session Start

Read `src/hardware/ic/imu/imu_notes.md` at the beginning of every session — it tracks
ongoing investigations, known hardware quirks, and design decisions for the IMU subsystem.

If the session involves NVS/NAND, also read `src/memory/nvs_notes.md`. If it involves the
USB host command protocol (dump/erase/rate-config, or anything in `src/comm/` or the
companion `wwd_gui_api` repo), also read `src/comm/protocol_notes.md`.

## Project Overview

WWD-n is a wearable device firmware project built on the Zephyr RTOS targeting the **nRF52833-QDAA** microcontroller (custom `nrf52833_ders` board). The project implements a multi-threaded system with display, IMU, buttons, NVS logging, and various peripherals.

Board: `nrf52833_ders/nrf52833` — board files in `boards/wwd/wwd_n/boards/Flavo/nrf52833_ders/`
BOARD_ROOT: `/home/anders/Documents/NCS/WWD-n` (passed via `-DBOARD_ROOT=` at configure time)

## Build System

This project uses Zephyr's CMake-based build system with the Nordic nRF Connect SDK (NCS).

### Building (nRF52833 target)

Claude can build directly:
```bash
export PATH="/home/anders/ncs/toolchains/b2ecd2435d/usr/local/bin:/home/anders/ncs/toolchains/b2ecd2435d/usr/bin:$PATH"
export LD_LIBRARY_PATH="/home/anders/ncs/toolchains/b2ecd2435d/usr/local/lib:/home/anders/ncs/toolchains/b2ecd2435d/usr/lib:$LD_LIBRARY_PATH"
export CMAKE_PREFIX_PATH=/home/anders/ncs/toolchains/b2ecd2435d/opt/zephyr-sdk
export ZEPHYR_BASE=/home/anders/ncs/v3.1.1/zephyr
# Configure (pristine):
cmake -B build_n33 -DBOARD=nrf52833_ders/nrf52833 -DBOARD_ROOT=/home/anders/Documents/NCS/WWD-n -GNinja
# Build:
cmake --build build_n33
```

The VS Code nRF Connect extension also works — set board to `nrf52833_ders/nrf52833` and board root to the repo root.

**Always do a pristine build** after any CMake, DTS, or board config changes.

### Debugging
The project includes debug scripts in `debug/`:
- `launch_all.sh` - Automated J-Link GDB server + GDB launch (interactive)
- `gdb_query.sh` - Non-interactive GDB batch executor for inspection and stepping (see below)
- VSCode debugging configured via `.vscode/launch.json` using nrf-connect extension

GDB debugging via command line:
```bash
cd debug
./launch_all.sh
```

## Hardware Observability

### UART Log Stream
The device UART output is captured by the host GUI application and written to:
```
/home/anders/Documents/GitHub/wwd_gui_api/data/text_data/
```
Files are named `TEXT_YYYYMMDDHHMMSS.log`. The most recent file is the current session.
Always check this alongside GDB output when debugging boot issues — it shows the full
init sequence with timestamps.

### GDB Batch Query Script (`debug/gdb_query.sh`)
Non-interactive GDB script for live device inspection without manual terminal work.
Starts a J-Link GDB server, connects, halts the target, runs commands, then resumes.
Output is saved to `debug/gdb_query_output.txt` and printed to stdout.

```bash
# Inspect live variables
debug/gdb_query.sh "p rtc_seconds" "p rtc_tick_hz"

# Dump peripheral registers (e.g. SPIM1 at 0x40004000)
debug/gdb_query.sh "x/16wx 0x40004000"

# Set breakpoints and step through a function
debug/gdb_query.sh \
  "break some_function" \
  "continue" \
  "next" \
  "print some_var" \
  "detach"

# Call a C function directly on-target
debug/gdb_query.sh \
  "break init_icm" \
  "continue" \
  "call readIMUReg(0x10075)" \
  "detach"
```

**Notes:**
- Uses `monitor reset halt` to stop the CPU before commands, then `detach` to resume
- Always kills any stale J-Link server on port 2331 before starting a fresh one
- `--batch` mode: GDB exits cleanly after all `-ex` commands run
- Breakpoints + `continue` work — the script waits for each breakpoint to fire
- Use `next` (step over) rather than `step` (step into) to avoid losing context in optimised builds
- `call func(args)` executes live C functions on the target — useful for reading registers
  via existing driver functions (e.g. `readIMUReg`)

## Lab Bench (MCP Tools)

Claude Code has direct access to bench equipment via an MCP server (`~/Documents/GitHub/wwd_gui_api/mcp_server.py`). All `mcp__lab__*` tools are pre-authorized — no prompts required.

### Equipment
| Equipment | Model | Connection |
|---|---|---|
| Power supply | SPD3303X | PyVISA (`@py` backend) — auto-connects at startup |
| DMM | XDM1041 | `/dev/ttyUSB0` — auto-connects at startup |
| USB relay | 4-ch HID | Auto-discovered |
| DAQ | USB-201 | 8-ch analog input, auto-discovered via uldaq |

### Target power
- **3.3 V on PS channel 1** — this is the board supply rail (`config/master.ini [Target] vdds = 3.3`)
- Use `ps_board_power(on)` to toggle board power, or `ps_output(channel=1, on=True/False)`

### Relay channel map
| Channel | Label | Purpose |
|---|---|---|
| 1 | ARDUINO | Arduino connection |
| 2 | — | Unused |
| 3 | MICRO-USB | Micro-USB switch |
| 4 | FTDI_IC | FTDI IC connection |

### GDB
Use `gdb_query(commands)` for live target inspection — no manual terminal needed. Always use `monitor halt` as the first command to stop the CPU before reading state. The script manages the J-Link GDB server lifecycle automatically.

```python
# Check where execution is
gdb_query(["monitor halt", "where"])

# Read a variable
gdb_query(["monitor halt", "p rtc_seconds"])

# Set breakpoint and wait for it
gdb_query(["break some_function", "continue", "bt", "detach"])
```

ELF: `~/Documents/NCS/WWD-n/build/zephyr/zephyr.elf`

## Architecture

### Threading Model
The application uses Zephyr's cooperative multithreading with semaphore-based synchronization:

- **clock_update_thread** (Priority 7): Updates clock, IMU temp, and step count every 9s (triggered by timer1_sem)
- **ui_refresh_thread** (Priority 7): Refreshes display every 1s when display is active (triggered by timer2_sem)
- **display_timeout_thread** (Priority 7): Handles display timeout after 9s (triggered by timer3_sem)
- **button_handler_thread** (Priority 5, highest): Handles button presses and IMU interrupts using k_poll on multiple semaphores

Main thread initialization order: LED → buttons → display → UI → interrupts → timers → sleep forever

### Module Organization

```
src/
├── main.c                    # Main application entry, thread definitions
├── circular_buffer.[ch]      # Generic circular buffer implementation
├── peripheral/              # Low-level peripheral drivers
│   ├── clock.[ch]          # Date/time arithmetic, get_current_time() wrapper
│   ├── rtc.[ch]            # Counter-based soft-RTC (nRF RTC2, dev boards)
│   ├── rv3028.[ch]         # RV-3028-C7 hardware RTC via Zephyr RTC API
│   ├── interrupt.[ch]      # GPIO interrupt configuration
│   └── timer.[ch]          # Timer setup and management
├── hardware/                # Hardware abstraction layer
│   ├── led.[ch]            # LED control
│   ├── button.[ch]         # Button handling with debouncing
│   └── ic/imu/             # IMU driver (see below)
├── display/                 # Display drivers and graphics
│   ├── display.[ch]        # Display abstraction layer (ST7735S only)
│   ├── st7735s/            # ST7735S driver — sole active display driver
│   ├── font.[ch]           # Font rendering
│   └── gfx/                # Graphics primitives
├── ui/                      # User interface logic
│   ├── ui.[ch]             # UI state machine and mode management
│   ├── ui_display.[ch]     # Display update functions
│   ├── ui_menu.[ch]        # Menu system
│   └── UIFunctions.c       # Individual UI function implementations
├── memory/                  # Storage drivers
│   ├── mt29f_nand.[ch]     # NAND flash driver
│   └── nvs.[ch]            # Non-volatile storage
├── comm/                    # Communication interfaces
│   ├── protocol.[ch]       # Binary host<->device command protocol (cdc_acm_uart1)
│   └── rate_config.[ch]    # Runtime IMU ODR / temperature log interval
├── ble/                     # Bluetooth LE (stub, inactive)
│   └── ble.[ch]            # Advertising + connection stub; enable via prj.conf
└── power/                   # Power management
    └── power.[ch]          # Battery ADC, VBAT_DIV_EN, BOOST_SEL, charging stub
```

### IMU Driver Architecture

The IMU subsystem has conditional compilation for different driver backends:

**CMake Options** (in `src/hardware/ic/imu/CMakeLists.txt`):
- `USE_DERS_IMU=ON` (default): Custom InvenSense driver with APEX features, FIFO, step counting
- `USE_ZEPHYR_IMU=ON`: Use Zephyr's built-in sensor API driver

When using `USE_DERS_IMU`, the following files are compiled:
- `imu.c` - High-level interface
- `ICM_42670.c` - IC-specific functions
- `inv_imu_driver.c` - InvenSense driver core
- `inv_imu_apex.c` - APEX motion processing features
- `imu_process.c` - Data processing
- `inv_imu_transport.c` - SPI transport layer
- `inv_time.c` - Timing utilities

### Display Driver

**ST7735S is the only display driver.** ST7789 files (`st7789.c`, `st7789.h`, `waveshare,st7789v2.yaml`) have been deleted. `display.c` calls `ST7735S_Init()` / `ST7735S_sleepOut()` / `ST7735S_sleepIn()` directly with no ifdefs. The `USE_ST7735S` macro has been removed from `display.h`.

### UI System

The UI operates as a state machine with modes defined in `ui_mode_t`:
- `UI_MODE_CLOCK` - Default clock display
- `UI_MODE_MENU` - Menu navigation
- `UI_MODE_PROMPT_TIME` - Time setting
- `UI_MODE_CHANGE_CONTRAST` - Contrast adjustment
- `UI_MODE_IMU_READ` - IMU data display
- `UI_MODE_IMU_TEMP` - Temperature display

UI updates use a dirty-flag optimization pattern - data structures track whether they need redrawing.

## Device Tree Configuration

Board DTS: `boards/wwd/wwd_n/boards/Flavo/nrf52833_ders/nrf52833_ders.dts`
Pinctrl: `boards/wwd/wwd_n/boards/Flavo/nrf52833_ders/nrf52833_ders-pinctrl.dtsi`

- **Console**: USB CDC ACM (`cdc_acm_uart0`) — no UART pins on nRF52833-QDAA
- **SPI1**: MT29F NAND (@0, CS P0.20), ICM42670P IMU (@1, CS P0.10), ST7735S (@2, CS P0.28)
- **I2C0**: MCP23008 GPIO expander @ 0x20 (SCL P0.30, SDA P1.09), RV-3028 RTC @ 0x52
- **IMU INT1**: P0.09 only — INT2 is not wired on the nRF52833 hardware (code guards with `#if IMU_HAS_INT2`)
- **RTC2**: enabled in DTS for 1-second software tick (`src/peripheral/rtc.c`)
- **ADC**: AIN2 (P0.04) for battery voltage via 1:2 divider, enabled by MCP23008 GP4

## Configuration

Key configuration in `prj.conf`:
- `CONFIG_DEBUG_OPTIMIZATIONS=y` and `CONFIG_NO_OPTIMIZATIONS=y` for debugging
- `CONFIG_SENSOR=y` for IMU support
- `CONFIG_DISPLAY=n` (display is manually driven, not using Zephyr display API)
- Stack debugging enabled: `CONFIG_STACK_SENTINEL`, `CONFIG_THREAD_STACK_INFO`, `CONFIG_INIT_STACKS`

**prj.conf comment rule**: Never remove or alter comments in `prj.conf` — commented-out lines are kept intentionally for future use. Preserve them verbatim, including lines like `# fucking memory debug`, `#CONFIG_LOG_BUFFER_SIZE=2048`, `# CONFIG_LOG_DEFAULT_LEVEL=4`, and `# this one is for printing thread stack space`.

### Logging Level Control

`LOG_MODULE_REGISTER(name, level)` — if `level` is set to a literal like `LOG_LEVEL_DBG`, it **overrides** `prj.conf`'s `CONFIG_LOG_DEFAULT_LEVEL` for that module regardless of build config. Use `CONFIG_LOG_DEFAULT_LEVEL` as the level argument so that `prj.conf` controls verbosity uniformly:

```c
LOG_MODULE_REGISTER(my_module, CONFIG_LOG_DEFAULT_LEVEL);
```

Hardcoded levels in individual modules have been a source of confusion — changing `LOG_LEVEL` in `prj.conf` had no effect because module-level overrides silently took precedence.

## Testing

UI function tests are available in `test/` directory. See `test/README.md` for detailed instructions.

Two methods for testing UI functions:
1. Temporarily replace `src/main.c` with test harness
2. Add CMake option `BUILD_UI_TESTS=ON` to conditionally compile tests

## Common Patterns

### Semaphore-Based Synchronization
Peripherals (buttons, timers, IMU) signal threads via semaphores. Button handler thread uses `k_poll()` to wait on multiple semaphores simultaneously.

### Interrupt Configuration
GPIO interrupts configured in `src/peripheral/interrupt.c`. Each interrupt has an associated semaphore that threads wait on.

### Hardware Initialization
Always initialize in this order to avoid dependency issues:
1. LEDs (for early feedback)
2. Buttons
3. Display (optional, can be disabled)
4. UI
5. Interrupts
6. Timers (triggers threads)

### Stack Size Tuning
Thread stack sizes defined in `src/main.c`. Use these commands to check stack usage:
```c
size_t free_stack;
k_thread_stack_space_get(&thread_name, &free_stack);
LOG_INF("thread_name free: %d", free_stack);
```

## NVS (Non-Volatile Storage) Implementation

**Read `src/memory/nvs_notes.md` before any NVS/NAND work** — it tracks the open metadata
bug, the shared-SPI1 question, and the fact that the MT29F is **not populated on SN2/SN3**.

### Flash Layout

The NAND flash is divided into two regions:
- **Data region**: blocks 0 to `META_BLOCK_START - 1` — stores log records
- **META region**: last 8 blocks (`META_BLOCK_COUNT = 8`) — stores metadata for write address recovery

### Data Structures

```c
struct log_state {        // Metadata written to META blocks
    uint32_t magic;       // 0xACACAC — identifies valid metadata
    uint32_t seq;         // Monotonically increasing sequence number
    uint64_t nand_offset; // Write address to resume from
    uint32_t crc;         // CRC32 over magic+seq+offset
};

struct log_entry_hdr {    // Prepended to every log record
    uint16_t record_type; // SAMPLE, TIME_ANCHOR, RESET_MARKER
    uint16_t length;      // Payload length in bytes
    uint16_t dt_ticks;    // Timestamp
};
```

### Startup Write Address Recovery

On boot, `nvs_init()` → `nvs_calc_offset()` → `nvs_read_metadata()`:

**Phase 1** — scans page 0 of all 8 META blocks, validates magic + CRC32, selects the block whose page 0 has the highest valid `seq` as the "active block".

**Phase 2** — scans pages 1, 2, … within the active block in order, advancing `best_state` for each valid page until an invalid magic or CRC is encountered (indicating an unwritten page).

The highest-seq entry found across both phases is authoritative. `write_addr` is restored from its `nand_offset`.

If no valid metadata found (fresh/erased flash): starts at offset 0 and writes an initial metadata entry.

**Recovery granularity caveat:** The recovered offset is only as current as the last metadata checkpoint. Checkpoints fire every 100 log records. If the device powers off between checkpoints, recovery will resume from the previous checkpoint — any records written after it but before power-off are lost (they were in the in-memory page buffer or flushed pages with no metadata update yet). Reducing the checkpoint interval (currently 100) improves recovery granularity at the cost of more META region wear.

### Metadata Wear Leveling

Each metadata write uses `seq % total_meta_pages` (where `total_meta_pages = META_BLOCK_COUNT * pages_per_block`) to select a specific page within the META region. Pages are written sequentially across all pages of all META blocks; a block is erased only when the rotation reaches its first page (`page_within_block == 0`). This distributes writes across all pages in all META blocks, not just one page per block.

Metadata is updated:
- On first init (fresh flash)
- Every 100 log records (periodic checkpoint)
- On `nvs_close()` (flush + update)

### Page Buffer

NAND flash requires full-page writes (2176 bytes). Records are accumulated in a static `page_buffer` and flushed to flash when:
- The buffer is too full to fit the next record
- A periodic metadata checkpoint fires (every 100 records)
- `nvs_flush_buffer()` / `nvs_close()` is called explicitly

Unfilled remainder of a page is padded with `0xFF` (erased NAND state).

### Write Flow

```
nvs_log_record()
  → copy hdr + payload into page_buffer
  → if buffer full: nvs_flush_page_buffer() → mt29f_write()
  → every 100 records: flush + nvs_write_metadata()
```

### IMU Sample Logging Gate

`NVS_LOG_IMU_SAMPLES` in `nvs.h` (0 or 1) controls whether `RECORD_IMU_FIFO` samples are written to flash. The call site in `imu.c` is wrapped in `#if NVS_LOG_IMU_SAMPLES`.

### Debugging NVS Contents

`nvs_dump()` reads all committed pages from offset 0 to `write_addr` and prints every record over `LOG_INF`. Only flushed data is visible — the in-memory page buffer is not included. Call it after `nvs_init()` during boot (before new records are written) to inspect a prior session's data.

## USB Host Command Protocol

`src/comm/protocol.c`/`.h` implement a binary host<->device command channel over
a **second** USB CDC ACM interface (`cdc_acm_uart1`, defined alongside the
existing console `cdc_acm_uart0` in `nrf52833_ders.dts`) — the console/log/shell
stream and the binary protocol never share a wire. Read `src/comm/protocol_notes.md`
before working on this; `protocol.h` is the canonical frame/command spec.

Commands implemented: `CMD_PING`, `CMD_DUMP_START/DATA/DONE` (streams the whole
committed NVS log back to the host with CRC32 verification), `CMD_ERASE` (chip
erase), `CMD_GET_RATE`/`CMD_SET_RATE` (runtime IMU ODR + temperature log
interval — see `src/comm/rate_config.c`).

`mt29f_nand.c` and `nvs.c` are now called concurrently from two threads (the
main pipeline loop and the protocol thread) and are protected by two mutexes
(`mt29f_bus_mutex`, `nvs_state_mutex`) — go through the existing public API
rather than touching NAND/NVS state directly, or new code won't be covered.

The GUI-side client lives in a companion repo: `/home/anders/Documents/GitHub/wwd_gui_api`
(`common/device_protocol.py`, `gui/guiTab_4_USB.py`, `common/dump_decoder.py`).
It hand-mirrors this repo's `protocol.h` enums — there is no shared source of
truth, so a protocol change here requires a matching edit there. That repo has
its own `CLAUDE.md`.

## Known Issues

- IMU INT1 (P0.9): Previously non-functional due to P0.9 being the NFC1 antenna pin — fixed by adding `nfct-pins-as-gpios` to `&uicr` in the device tree. Both INT1 and INT2 require push-pull configuration on the IMU side.
  - **This fix was lost in the port to `nrf52833_ders` and restored 2026-07-26.** P0.10 (NFC2) is the IMU's *chip select* on this board, so losing it broke the IMU entirely, not just the interrupt. On nRF52 the DTS property alone is a no-op — `system_nrf52.c` gates the UICR write on `CONFIG_NFCT_PINS_AS_GPIOS`, which is now set in `nrf52833_ders_defconfig`. Keep both. See `imu_notes.md`.
- ~~IMU init fails when NVS runs first (SPI bus contention, UNRESOLVED)~~ — **does NOT reproduce on the nRF52833 boards.** That analysis was from the previous nRF52832 BETA board. Re-tested from scratch on SN3 (MT29F populated 2026-07-26): `WHO_AM_I` reads correctly after `nvs_init()` on every boot, including under continuous NAND write load and concurrent USB-protocol-driven flash dumps. See `imu_notes.md` and `src/memory/nvs_notes.md` for the re-test.
- NVS/NAND logging is implemented (`NVS_LOG_IMU_SAMPLES=1` in nvs.h) and **extensively tested on SN3** (MT29F populated 2026-07-26): full pipeline (IMU FIFO + temperature + metadata rotation through both even and odd META blocks), a MT29F plane-select aliasing bug found and fixed (see `src/memory/nvs_notes.md`), and multi-MB flash dumps verified byte-for-byte over the USB host command protocol (see `src/comm/protocol_notes.md`). Still untested: SN2 (MT29F not populated there).
- Display timeout thread code exists but is currently commented out in main.c
- BMS (battery management) code is stubbed out but not implemented
- `clock_set_time()` in UIFunctions.c — the "confirm time" UI action needs to be wired to call `clock_set_time(time_offset)` (and ultimately `rv3028_set_time()` once hardware is ready)
- `get_current_time()` in `clock.c` currently calls `rtc_get_time()` (counter-based) — swap to `rv3028_get_time()` for production once RV-3028 hardware is verified
