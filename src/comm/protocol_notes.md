# USB Host Command Protocol — Notes

Companion to `src/memory/nvs_notes.md` and `src/hardware/ic/imu/imu_notes.md`.
Read this before touching `src/comm/` or the GUI-side client in `wwd_gui_api`.

`protocol.h` is the canonical spec (frame format, command IDs, error codes) —
this file is gotchas, history, and the cross-repo pointer. Don't duplicate the
spec here; if protocol.h and this file ever disagree, protocol.h wins.

---

## [BUILT 2026-07-26] cdc_acm_uart1 command channel — PING/DUMP/ERASE/GET+SET_RATE all working

Built from scratch this session: the board previously had no host->device
command channel at all (`src/comm/uart.c`, now deleted, was a dead, never-built,
never-called TX-only sketch). Second USB CDC ACM interface added in
`nrf52833_ders.dts` (`cdc_acm_uart1`) so the binary protocol never has to share
a wire with the human-readable console/log/shell stream on `cdc_acm_uart0`.

Verified end-to-end against real hardware (SN3): PING round-trip, a full
~2.2 MB DUMP with per-frame and whole-dump CRC32 checks (matches Python's
`zlib.crc32` exactly — see `frame_crc()` / the CRC seed choice below), ERASE,
and GET/SET_RATE including a rejected out-of-range SET_RATE leaving state
untouched. GUI-side client + dump decoder/viewer live in the companion repo —
see "Companion GUI repo" below.

### Gotchas that cost real time this session

1. **`uart_fifo_fill()` corrupted large payloads.** First DUMP implementation
   batched TX through `uart_fifo_fill()` (interrupt-driven, backed by cdc_acm's
   own TX ring buffer + workqueue) instead of `uart_poll_out()`, for speed.
   Multi-KB frames came back with CRC mismatches that were NOT reproducible in
   a stable way — the device's own per-frame CRC (computed and logged
   immediately before transmission) was internally consistent run-to-run for
   the same static flash content, but the bytes the host actually received
   differed between runs. That points at the ring-buffer producer
   (`cdc_acm_fifo_fill` → `ring_buf_put`) / consumer (`tx_work_handler` on
   `USB_WORK_Q`) path, not at the framing or CRC logic. Root cause not fully
   chased down (plausible: cdc_acm.c's `CONFIG_USB_CDC_ACM_RINGBUF_SIZE`
   wrap-around under a very fast producer). **Fix: went back to
   `uart_poll_out()` one byte at a time** (proven correct all session,
   including every PING/ACK), with a `k_yield()` every 64 bytes so the USB
   stack's own workqueue doesn't get starved on a large payload — that
   starvation is what was knocking the device off the USB bus during the
   pre-fix DUMP attempts (looked like intermittent hangs/disconnects, was
   actually just very slow — a full 2.2 MB dump takes ~90 s at this rate).
   **If revisiting `uart_fifo_fill` for throughput, prove it first against a
   multi-KB payload, not just small ACKs — small payloads never triggered
   this.**

2. **`mt29f_read()`/`mt29f_write()` require exact page-size-multiple lengths**
   (2176 B) — `nvs_read()` used to silently swallow the resulting `-EINVAL`
   from a non-aligned length and return success anyway (fixed: now propagates
   the real return code). The first DUMP chunk size (256 B) violated this;
   fixed by always reading one full page and trimming only the last page's
   *transmitted* length to `total - addr`, never the actual flash read length.

3. **New cross-thread concurrency, now mutex-protected.** Before this
   session, `mt29f_nand.c` / `nvs.c` were only ever called from one thread
   (the main loop). The protocol thread (DUMP/ERASE handlers) is a second
   caller now, running concurrently with the pipeline's `nvs_log_record()`
   every second. Two mutexes were added:
   - `mt29f_bus_mutex` (`mt29f_nand.c`) — guards the actual SPI bus /
     `mt29f_read/write/block_erase/chip_erase/chip_reset`.
   - `nvs_state_mutex` (`nvs.c`) — guards `write_addr`/`page_buffer`/
     `metadata_seq` via `nvs_log_record`, `nvs_flush_buffer`,
     `nvs_write_metadata`, `nvs_erase_chip`.
   Both are recursive (`k_mutex`), so nesting (e.g. `nvs_log_record()` calling
   `nvs_flush_page_buffer()` calling `nvs_write_metadata()`) is safe. **Any
   new code that touches the NAND/NVS state must go through the existing
   public API** (`mt29f_*` / `nvs_*`) rather than reaching around it, or it
   won't be covered by these locks.

4. **Test harness footgun: DUMP/ERASE are not cancelable.** If a host-side
   test script aborts early (timeout too short, Ctrl-C, whatever), the device
   keeps streaming/erasing regardless — there's no host->device "cancel"
   frame. The *next* connection attempt will see the tail of the previous
   operation's frames interleaved with the new one's responses, which looks
   exactly like data corruption but isn't. Either let an operation finish
   before starting a new one, or power-cycle/reflash between attempts when
   debugging at the raw-protocol level. GDB `monitor halt`/reset makes this
   worse, not better — see `nvs_notes.md`'s existing warning about GDB
   resetting the target on connect.

### Command status (see protocol.h for the actual spec)

| Command | Status |
|---|---|
| `CMD_PING` | done, verified |
| `CMD_DUMP_START/DATA/DONE` | done, verified (~90 s for 2.2 MB @ current poll_out rate) |
| `CMD_ERASE` | done, verified (blocking, several seconds) |
| `CMD_GET_RATE` / `CMD_SET_RATE` | done, verified. IMU ODR (25/50/100/200/400/800 Hz, `imu_set_odr()`) + temperature log interval (1-3600 s, `rate_config.c`). FSR (16 g / 2000 dps) is still hardcoded — not exposed to the protocol. |

### Companion GUI repo

`/home/anders/Documents/GitHub/wwd_gui_api` — `common/device_protocol.py` is
the host-side client. **It mirrors protocol.h's frame format and command/error
enums by hand — there is no shared source of truth or codegen.** If
`enum protocol_cmd` or `enum protocol_err` in `protocol.h` changes, update
`device_protocol.py`'s `CMD_*` / `ERR_NAMES` to match, or the host will
silently misparse. GUI wiring is in `gui/guiTab_4_USB.py` (the "Device
Protocol" panel on the USB COMM tab); dump decoding/plotting is
`common/dump_decoder.py`. That repo has its own `CLAUDE.md` — read it before
GUI-side protocol work, same as this file before firmware-side work.
