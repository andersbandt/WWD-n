# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

WWD-n is a wearable device firmware project built on the Zephyr RTOS targeting the nRF52832 microcontroller (96b_nitrogen board). The project implements a multi-threaded system with display, IMU, buttons, and various peripherals.

## Build System

This project uses Zephyr's CMake-based build system with the Nordic nRF Connect SDK (NCS).

### Building
```bash
west build -b 96b_nitrogen/nrf52832
```

### Flashing
```bash
west flash
```

### Clean Build
```bash
rm -rf build
west build -b 96b_nitrogen/nrf52832
```

### Debug Build Script (Recommended)
The project includes a debug build script that performs a pristine build with debug optimizations enabled:
```bash
cd debug
./compile.sh
```

This script runs:
```bash
west build --build-dir /home/anders/Documents/NCS/WWD-n/build /home/anders/Documents/NCS/WWD-n --pristine --board 96b_nitrogen/nrf52832 --no-sysbuild -- -DCONF_FILE=prj.conf -DCONFIG_DEBUG_OPTIMIZATIONS=y
```

Use this script to check for compile errors and ensure a clean build from the debug directory.

### Debugging
The project includes debug scripts in `debug/`:
- `launch_all.sh` - Automated J-Link GDB server + GDB launch
- VSCode debugging configured via `.vscode/launch.json` using nrf-connect extension

GDB debugging via command line:
```bash
cd debug
./launch_all.sh
```

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
├── clock.[ch]               # Clock/time management
├── peripheral/              # Low-level peripheral drivers
│   ├── interrupt.c          # GPIO interrupt configuration
│   └── timer.c              # Timer setup and management
├── hardware/                # Hardware abstraction layer
│   ├── led.[ch]            # LED control
│   ├── button.[ch]         # Button handling with debouncing
│   └── ic/imu/             # IMU driver (see below)
├── display/                 # Display drivers and graphics
│   ├── display.[ch]        # Display abstraction layer
│   ├── st7735s/            # ST7735S driver (default)
│   ├── st7789.c            # ST7789 driver
│   ├── gfx.c               # Graphics primitives
│   └── fonts.c             # Font rendering
├── ui/                      # User interface logic
│   ├── ui.[ch]             # UI state machine and mode management
│   ├── ui_display.[ch]     # Display update functions
│   ├── ui_menu.[ch]        # Menu system
│   └── UIFunctions.c       # Individual UI function implementations
├── memory/                  # Storage drivers
│   ├── mt29f_nand.[ch]     # NAND flash driver
│   └── nvs.[ch]            # Non-volatile storage
├── comm/                    # Communication interfaces
│   └── uart.[ch]           # UART driver
└── power/                   # Power management
    └── power.[ch]          # Power control
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

### Display Driver Selection

Display driver is selected via `#define USE_ST7735S` in `src/display/display.h`:
- **ST7735S** (default): 128x160 display
- **ST7789**: Alternative display controller

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

Hardware configuration is in `boards/96b_nitrogen_nrf52832.overlay`:
- SPI1 peripherals: MT29F NAND (@0), ICM42670P IMU (@1), ST7789 display (@2), ST7735S display (@3)
- IMU interrupts on GPIO P0.9 (INT1) and P0.3 (INT2) — P0.9 is the NFC1 antenna pin on nRF52832 and requires `nfct-pins-as-gpios` in the `&uicr` device tree node (prj.conf alone is not sufficient):
  ```
  &uicr {
      gpio-as-nreset;
      nfct-pins-as-gpios;
  };
  ```
- Display control pins configured per device

## Configuration

Key configuration in `prj.conf`:
- `CONFIG_DEBUG_OPTIMIZATIONS=y` and `CONFIG_NO_OPTIMIZATIONS=y` for debugging
- `CONFIG_SENSOR=y` for IMU support
- `CONFIG_DISPLAY=n` (display is manually driven, not using Zephyr display API)
- Stack debugging enabled: `CONFIG_STACK_SENTINEL`, `CONFIG_THREAD_STACK_INFO`, `CONFIG_INIT_STACKS`

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

1. Scans all 8 META blocks, reads first page of each
2. Validates magic (`0xACACAC`) and CRC32 for each candidate
3. Selects the valid entry with the **highest sequence number** as the authoritative state
4. Restores `write_addr` from `nand_offset` in that entry
5. If no valid metadata found (fresh/erased flash): starts at offset 0 and writes an initial metadata entry

### Metadata Wear Leveling

Each metadata write uses `seq % META_BLOCK_COUNT` to select which of the 8 META blocks to write to. This rotates writes across all blocks so no single block wears out from repeated metadata updates.

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

## Known Issues

- IMU INT1 (P0.9): Previously non-functional due to P0.9 being the NFC1 antenna pin — fixed by adding `nfct-pins-as-gpios` to `&uicr` in the device tree. Both INT1 and INT2 require push-pull configuration on the IMU side.
- NAND flash support is implemented but not actively used in main application
- Display timeout thread code exists but is currently commented out in main.c
- BMS (battery management) code is stubbed out but not implemented
