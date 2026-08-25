# Stack Budget

Written 2026-08-24 during the changeover from `CONFIG_NO_OPTIMIZATIONS=y` to
`CONFIG_SIZE_OPTIMIZATIONS=y` (see the comment block at the bottom of `prj.conf`).

The project has lost a full session to a stack overflow before
(`display_timeout_thread`, 512 B, and later `k_sys_work_q`), and the standing
warning was that changing optimization level *shifts* stack usage. It does —
but it shifts it **down**, substantially, and the measurement also turned up two
threads that were already at or over their limit on the old build.

## How to reproduce these numbers

Static worst-case call-depth analysis. Two ingredients:

1. `-fstack-usage` emits a `.su` file next to every object, giving each
   function's own frame size.
2. `arm-zephyr-eabi-objdump -d` gives the call graph (`bl`/`blx` targets).

Walk the graph from each thread entry point summing frames, taking the max at
each branch, breaking cycles. Script: `debug/stack_depth.py`.

```bash
cmake -B build_su -DBOARD=nrf52833_ders/nrf52833 \
      -DBOARD_ROOT=/home/anders/Documents/NCS/WWD-n \
      -DEXTRA_CONF_FILE=debug/stack_usage.conf -GNinja
cmake --build build_su
python3 debug/stack_depth.py build_su/zephyr/zephyr.elf build_su
```

## Results

Production (`prj.conf`, `-Os`):

| thread | stack | worst case | used |
|---|---|---|---|
| `main` | 4096 | 712 B | 17.4% |
| `sensor_update_thread` | 4096 | 288 B | 7.0% |
| `ui_refresh_thread` | 4096 | 480 B | 11.7% |
| `button_handler_thread` | 4096 | 584 B | 14.3% |
| `protocol_thread` | 4096 | 432 B | 10.5% |

Old debug build (`debug.conf`, `NO_OPTIMIZATIONS`), at the **pre-2026-08-24**
stack sizes:

| thread | stack | worst case | used |
|---|---|---|---|
| `main` | 4096 | 2520 B | 61.5% |
| `sensor_update_thread` | 2048 | 1976 B | **96.5%** |
| `ui_refresh_thread` | 4096 | 2448 B | 59.8% |
| `button_handler_thread` | 4096 | 2592 B | 63.3% |
| `protocol_thread` | 2048 | 2448 B | **119.5% — over** |

`-Os` cuts worst-case depth by **3–5x**. Inlining collapses 3299 functions into
1119, so individual frames get *bigger* (`mt29f_init` 80 → 240 B,
`nvs_config_load` 24 → 160 B) while the chains they sit in get much shorter. The
chain length wins by a wide margin.

## The two problems this found in the OLD build

Both were pre-existing and unrelated to optimization level. Both are fixed by
stack bumps (`src/main.c`, `src/comm/protocol.c`), each with the reasoning
inline at the `#define`.

- **`sensor_update_thread` at 96.5%** — ~72 B of margin, on the 9 s pedometer
  tick, a path that runs constantly.
- **`protocol_thread` statically over its stack** — and reachable, not a
  phantom. `ui_show_dump_in_progress(false)` calls `ui_refresh()`, which
  dispatches on the current `ui_mode`. Run a USB dump with the **Data Stats
  screen up** and that screen's draw path executes on the protocol thread's
  stack instead of `ui_refresh_thread`'s.

  The bump makes it safe; it does not make it *right*. A protocol thread
  reaching into UI drawing is the actual defect — the end-of-dump restore
  should post to the UI thread rather than draw inline. Worth doing when
  `protocol.c` is next open.

## What this analysis does NOT cover

Read these before trusting the table as a bound:

- **Indirect calls are invisible.** Anything dispatched through a function
  pointer — the InvenSense `serif->read_reg`/`write_reg` transport indirection,
  Zephyr's `struct device` driver APIs, work-queue handlers, k_timer callbacks
  — is not an edge in the call graph. Chains that cross one of those are
  **truncated**, so the real depth can exceed these numbers.
- **Path feasibility is not checked.** The deepest chain may be unreachable
  (mutually-exclusive branches), which pushes the other way — the numbers are
  pessimistic in this respect and optimistic in the one above.
- **System threads are not listed.** `main`'s figure is the application entry
  path only. `k_sys_work_q`, the two USB workqueues, and the logging thread are
  configured in `prj.conf` and have their own history (see the
  `CONFIG_SYSTEM_WORKQUEUE_STACK_SIZE` comment there).
- **ISRs do not nest on these stacks.** Cortex-M runs handlers on MSP
  (`CONFIG_ISR_STACK_SIZE=2048`) while threads run on PSP. Only the exception
  entry frame lands on the thread stack, and with no hardware FPU on this build
  (`CONFIG_FPU` unset) that is the basic 8-word / 32 B frame.

**Because of the first point, this analysis does not replace a hardware run.**
Confirm with `stackcheck.conf` (Zephyr thread analyzer, prints every thread's
high-water mark including the system ones) while exercising the deep paths:
every button, the whole menu, display sleep/wake, and a USB dump started from
the Data Stats screen specifically.

## Hardware confirmation — SN3, 2026-08-24, PASSED

Measured with `stackcheck.conf` on SN3 (`1055205CB9AEDAEC`), production `-Os`
build, over the USB CDC console. Exercised: full boot with NVS metadata
recovery, IMU FIFO logging at 100 Hz, display sleep/wake, the whole button menu
walked by hand, and a real 972,672-byte USB dump completed **while the Log Stats
screen was displayed** — the specific chain that measured 119.5% statically.

| thread | stack | measured peak | used | static estimate |
|---|---|---|---|---|
| `logging` | 2048 | 560 B | 27.3% | — |
| `thread_analyzer` | 2048 | 520 B | 25.4% | — (tool only) |
| `button_handler` | 4096 | **952 B** | 23.2% | 584 B |
| `main` | 4096 | 856 B | 20.9% | 712 B |
| `ui_refresh` | 4096 | 752 B | 18.4% | 480 B |
| `ISR0` | 2048 | 328 B | 16.0% | — |
| `idle` | 320 | 48 B | 15.0% | — |
| `protocol` | 4096 | **536 B** | 13.1% | 432 B |
| `usbd_workq` | 4096 | 480 B | 11.7% | — |
| `sensor_update` | 4096 | 464 B | 11.3% | 288 B |
| `sysworkq` | 4096 | 360 B | 8.8% | — |
| `usbworkq` | 4096 | 240 B | 5.9% | — |

**Worst thread on the whole system is 27.3%.** The production build has a
3.7x margin on its tightest stack.

**Measured exceeds static on every app thread** (`button_handler` 952 vs 584,
`ui_refresh` 752 vs 480). That is the indirect-call blind spot behaving exactly
as described above — treat the static tool as a ranking device and a regression
check, never as an upper bound.

The **debug build was not re-measured on hardware** — its 119.5% figure remains
static-only. The bump to 4096 covers its 2448 B static worst case with room to
spare, but if `debug.conf` ever becomes load-bearing again, measure it.

### Two things this session turned up that are worth fixing

- **The analyzer's own output was being silently truncated.** Rows went missing
  from the middle of each report (`ui_refresh` and `button_handler` absent while
  `sensor_update` and `protocol` appeared) with `--- N messages dropped ---` in
  the stream. Cause is `CONFIG_LOG_BUFFER_SIZE`, not RTT — the default 1024 B
  cannot hold one analyzer burst, so Zephyr drops the tail *before any backend
  sees it*. `stackcheck.conf` now sets 8192. Widening the RTT buffer alone does
  not fix it. Anyone reading a big burst of log output on this project should
  know this failure mode: it looks like a hardware or probe problem and isn't.
- **A full NAND produces an unthrottled error log at the IMU ODR.** With the
  flash full, `nvs` emitted one `<err> Write would exceed data region` per
  rejected record at 100 Hz. That floods the log buffer, starves every other
  message, and burns CPU and power for no diagnostic value after the first one.
  Wants a log-once latch or a rate limit. Unrelated to stacks; found because it
  made the board unmeasurable until the flash was erased.
