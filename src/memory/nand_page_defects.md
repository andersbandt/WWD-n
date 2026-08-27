# MT29F page-layout defects found 2026-08-26

Investigation write-up. Triggered by a wall of
`dump_decoder: record overruns page N at offset M` warnings while decoding
`DUMP_20260826220916_first_running_activity.bin` (206 MB, 94,964 pages, SN3).

**Nothing here is fixed yet.** This is the evidence, the two defects it points
at, the experiments that would confirm or kill each one, and the fixes I'd
propose. Audit tool: `debug/nand_page_audit.py` (re-runnable against any dump).

Supersedes part of *[ANALYZED 2026-08-07] Torn NAND page writes* in
`nvs_notes.md` — see "What this means for the 2026-08-07 note" below.

---

## 0. What the warning itself means

`dump_decoder.py:134`. Walking a 2176-byte page, the decoder read a record
header whose `length` would run past the end of the page. The firmware never
splits a record across pages (`nvs_log_record()` flushes the buffer when the
record won't fit), so this cannot happen in a healthy log. The decoder treats
the page's record stream as untrustworthy from that point, logs the warning,
and **`break`s — silently discarding the remainder of that page**.

So each warning line = one page truncated at that offset. It is a symptom
report, not a decoder bug.

---

## 1. Evidence

All figures from `debug/nand_page_audit.py`, sampling every 7th page of the
dump above (13,565 written pages sampled; 4.7% unparseable).

### 1a. The last 128 bytes of every page never come back

For **every** parseable page sampled, the record stream ends somewhere in
1984..2047 — never once past 2048, in 12,928 pages. And on **every** written
page, the spare region is non-`0xFF` starting at **exactly byte 2112**
(941/941 in a separate sweep; 3063/3064 at stride 31), running to 2175, even
though `nvs_flush_page_buffer()` pads the buffer with `0xFF` out to 2176.

The firmware cannot have produced either pattern:

- `nvs.c:48` `page_buffer[2176]`, and `nvs_log_record()` packs records until
  `page_buffer_offset + total_size > cfg->bytes_per_page` (2176). With the
  20-byte IMU_FIFO record that fills to ~2160, not to <2048.
- Firmware writes `0xFF` at 2112..2175. Something else put data there.

That something is the chip. `mt29f_nand.c:71` declares `bytes_per_page = 2176`
and `mt29f_nand.c:644` sets `SEC_STATUS_BIT_ECC_EN`. With internal ECC enabled
the MT29F2G01's spare area is **chip-managed**: the 2048-byte main area is four
512-byte ECC sectors, and their parity lands in the last 64 bytes of spare. The
observed layout matches exactly:

| bytes | what actually lives there | what the driver thinks |
|---|---|---|
| 0..2047 | main data, 4 x 512 B ECC sectors | user data |
| 2048..2111 | user spare, chip-managed under ECC | user data |
| 2112..2175 | **ECC parity, 4 x 16 B, chip-written** | user data |

Supporting detail: 5 of 941 sampled pages had parity in only 2112..2127 — one
16-byte parity block, i.e. a page where only the first 512-byte sector carried
data. That is the 4x16 structure showing through.

### 1b. Corrupt pages have bits *cleared*, never set

On a corrupt page the 20-byte IMU_FIFO record grid is still visibly intact —
`record_type` and `dt_ticks` stay plausible, and only the `length` field goes
wrong. Across seven corrupt pages, of the length fields that were not the
correct `0x000e`, **348 were bit-subsets of `0x000e` and 14 were not** (96%).
The observed values are `0x0c, 0x0a, 0x08, 0x06, 0x04, 0x02, 0x00` — every one
a subset. Bits only ever go 1 -> 0.

Corroborating: the ECC parity region of corrupt pages carries measurably fewer
1-bits than that of clean pages (mean 146.8 vs 166.4 of 512).

### 1c. Corruption is clustered in time, not uniform

Sampling every 17th page, by page range:

```
     0- 9999: 60/589 bad      50000-59999:  0/588
 10000-19999: 47/588          60000-69999: 61/587
 20000-29999:  2/588          70000-79999: 76/588
 30000-39999:  0/588          80000-89999: 11/589
 40000-49999:  0/589          90000-99999:  2/292
```

Long stretches are perfectly clean; other stretches run to 13% bad. A defect
inherent to every write would not do that.

---

## 2. Defect A — the page tail is silently discarded (CONFIRMED)

`bytes_per_page = 2176` is correct as the **address stride** (offset -> row
address) and as the SPI transfer length, but wrong as the **payload capacity**.
With ECC enabled the chip owns bytes 2048..2175, so the last 128 bytes of every
`page_buffer` are thrown away.

Consequences, both silent today:

1. **~5.9% of all logged records are lost** — whatever the packer put in
   2048..2175 never reaches flash. Section 1a is that loss, measured.
2. **A record straddling offset 2048 is half-written.** Its header survives in
   the main area, its payload is truncated and reads back as `0xFF`. The
   decoder's bounds check passes (the length still fits inside 2176), so it
   emits a record with a garbage payload and no warning at all. These are worse
   than the overruns, because nothing flags them.

`nvs_bringup.c`'s `plane_alias_test()` writes `0xAA`/`0x55` across all 2176
bytes and `memcmp`s the full page — under this model that comparison **cannot**
pass. If it has been reporting `correct=1`, this model is wrong and I want to
know before touching anything. That is test T1 below.

## 3. Defect B — pages are being programmed twice without an erase (HIGH CONFIDENCE)

NAND programming only clears bits, so programming a second time over live data
yields `old AND new`. That is precisely the signature in 1b, and it explains
the clustering in 1c (it happens only after whatever event rewinds the write
pointer, not on every write).

It also explains the exact shape of the damage, which bit rot does not:

- Two IMU_FIFO streams programmed at the same 20-byte alignment AND together
  into a **still-valid-looking** record: `02 00 0e 00` AND `02 00 0e 00` is
  unchanged, so `record_type` and `length` survive and only the payload and
  `dt_ticks` are quietly wrong.
- As soon as one of the two streams contains a differently-sized record (an
  18-byte `TIME_ANCHOR`, a `ACTIVITY`), the two grids shift out of phase, the
  `length` field starts ANDing against payload bytes, and the walk desyncs and
  overruns.

Both observed pages match this to the byte: page 94658 opens with an 18-byte
`TIME_ANCHOR` and is garbage from offset 18 onward; page 81638 parses 11 clean
IMU records and goes bad at offset 220.

**Suspected cause — not yet confirmed:** `write_addr` recovery rewinding onto
already-written pages. `nvs_calc_offset()` -> `nvs_read_metadata()` restores the
offset from the newest valid metadata checkpoint, and CLAUDE.md already documents
that this is only as current as the last checkpoint. A small rewind (checkpoints
fire every 100 records, ~1 page) would corrupt ~1 page per boot, which is far too
few for the ranges in 1c. A *large* rewind — a stale metadata entry winning the
seq comparison — would produce exactly those ranges. That points straight at the
still-open META rotation / `meta_seq=64` even-odd question in `nvs_notes.md`.

Alternative causes not yet excluded: a `CMD_ERASE` that left blocks unerased; the
~5 reflashes during this capture resuming onto the same pages.

---

## 4. What this means for the 2026-08-07 note

`nvs_notes.md`'s *[ANALYZED 2026-08-07] Torn NAND page writes* attributed the
same warning to torn/partial page programs from power loss at boot boundaries.
That conclusion needs revising: a torn write leaves bits **un**programmed
(reading as 1 / `0xFF`), and what is on flash here is the opposite — bits
cleared, in a page whose record grid is otherwise intact. The boot-boundary
correlation that note measured is still real and still points at reboots; the
mechanism is a *re-program after a pointer rewind*, not a torn program.

The note's decoder-side recommendation (clamp implausible per-segment durations
before summing) stands independently and is still worth doing.

---

## 5. Third finding — ECC status is never read

`mt29f_defs.h:58-67` defines `STATUS_BIT_ECCS0/1/2` and the `ECC_STATUS_*`
codes. `grep -n "ECCS\|ECC_STATUS" src/memory/*.c` returns **nothing** — no read
path anywhere checks them. `spi_nand_page_write()` does check `P_FAIL`
(`mt29f_nand.c:585`) and `mt29f_block_erase()` checks `E_FAIL`, so the read side
is the only gap. A page that came back uncorrectable has been indistinguishable
from a good one since day one, which is why both defects above were invisible
until a dump was decoded.

---

## 6. Test plan

Ordered cheapest-first. T1 and T2 settle defect A; T3/T4 settle defect B. All
run on SN3 (NAND known good) from the production build.

**T1 — does the existing plane test even pass?** (2 min, no code)
Run the `nvs_bringup.c` plane-alias test and read its `correct=` lines. It
`memcmp`s all 2176 bytes of an `0xAA` page. Model predicts `correct=0` with the
first mismatch at byte 2112. If it prints `correct=1`, stop: defect A is wrong.

**T2 — known-pattern page round-trip.** (small addition to `nvs_bringup.c`)
Erase a scratch block in the data region. Write one page filled with a
byte-position-derived pattern (e.g. `buf[i] = i & 0xFF` — never `0xFF`, so
"discarded" is distinguishable from "written"). Read it back and report the
first mismatching offset and a hexdump of 2040..2175. Expected: bytes 0..2047
match, 2048..2111 read `0xFF`, 2112..2175 are unrelated parity. That pins the
usable payload boundary exactly, which is the number the fix needs.

**T3 — read-side ECC status.** (small addition to `spi_nand_page_read()`)
After `spi_nand_page_cache_read()`, `get_feature(REG_STATUS)` and decode
ECCS[2:0]. Log anything other than `ECC_STATUS_OK`. Then re-read a *known
corrupt* page from the current dump by its page index (91638 / 94658 are on SN3
now, assuming no erase since) and see what the chip says about it. Uncorrectable
would be strong independent confirmation that those pages are physically
damaged rather than mis-decoded. Note the ECCS bit ordering wants a datasheet
check — the local `ECC_STATUS_*` values are 3-bit codes and I have not verified
the shift.

**T4 — catch the rewind in the act.** (instrumentation, no behaviour change)
In `nvs_init()`, after `nvs_calc_offset()` recovers `write_addr`, read the first
6 bytes of the page at that offset. If they are not `0xFF 0xFF ...`, the
recovered offset points at a **live page** and the next write will double-program
it. Log offset, recovered `seq`, and the bytes found. This is the decisive test:
if it fires on a normal reboot, defect B is confirmed and quantified in one boot
cycle, and the log tells us how far the rewind went.

---

## 7. Proposed fixes

### A: cap the payload at the main area, keep the stride

Add a distinct field — `usable_bytes_per_page = 2048` — used **only** by
`nvs.c`'s packing decisions (`nvs_log_record()`'s fit check,
`nvs_flush_page_buffer()`'s pad length, `nvs_dump()`'s walk bound). Keep
`bytes_per_page = 2176` for the offset->row-address arithmetic and the SPI
transfer, and keep padding the buffer to 2176 with `0xFF` so the chip programs
parity onto erased bits.

The nice property: **this is not an on-flash layout change.** Page addresses,
page size, and the dump format are unchanged; the packer just stops 128 bytes
earlier. Old dumps keep decoding, and `dump_decoder.py` needs **no change at
all** — it already terminates a page walk on the first `0xFF`, which it will now
hit at <=2048 instead of running into parity. (I said in-session that this would
be a format change needing a versioned decoder; reading the code, it is not.)

Cost: the 5.9% of capacity we are *already* losing today, made explicit. Nothing
else regresses. Contingent on T2 confirming the boundary is 2048 and not 2112.

### B: resume on the first erased page, not on the checkpoint

Fixes the rewind at the root and also retires the "recovery granularity caveat"
in CLAUDE.md. After `nvs_read_metadata()` recovers a candidate `write_addr`,
scan forward page by page while the page's first 6 bytes are not `0xFF`,
stopping at the first erased page and resuming there. Cost is a handful of page
reads at boot, bounded by how far the checkpoint lagged. Benefits:

- a page is never programmed twice, whatever the metadata said;
- records written after the last checkpoint are **recovered** rather than
  overwritten — today they are lost *and* they corrupt what overwrites them;
- it degrades safely if metadata is stale by an arbitrary amount.

Should be gated on T4's finding so we fix the mechanism we actually have.

### C: check ECCS on every read

Independent of both, and cheap: one `get_feature` per page read. Return `-EIO`
on `ECC_STATUS_NOT_OK` and log-once on the correctable-but-degrading codes.
Without this the next silent corruption is invisible in exactly the same way.

### D (decoder, optional): stop discarding the rest of the page

`decode_dump()` currently `break`s on the first overrun. It could instead
re-scan forward for the next plausible header and carry on, tagging recovered
rows. Only worth doing if we want to salvage data from the existing dumps —
after fixes A and B, healthy captures will not need it.

---

## 8. Open questions

- Is the usable boundary 2048 or 2112? T2 answers it. The user-spare region may
  be writable in 16-byte chunks with ECC on; not worth the complexity for 64 B.
- How much of the current dump is recoverable? Pages corrupted by defect B hold
  the AND of two streams and are not invertible. Records lost to defect A are
  simply gone.
- Does SN3's NAND need a full erase before the next real capture? Probably yes,
  and defect B means partially-written blocks are the risk area.
- The 5 pages with a single 16-byte parity block are unexplained in detail —
  harmless, but they are the best clue to the exact sector/parity mapping if T2
  is ambiguous.

---

# RESULTS — bench run on SN3, 2026-08-26

Everything below is measured, not inferred. The probe lives in
`nvs_bringup.c` behind `NVS_PAGE_PROBE`; its console output is quoted verbatim.

**Both defects confirmed. Both fixed and verified on hardware. A third defect
was found along the way (§R5) and is NOT fixed.**

## R1 — T1: the existing plane test was reporting a fault that does not exist

```
[plane test] even first mismatch at byte 2112
[plane test] even block correct=0 (first byte=0xaa)
[plane test] odd block reads even pattern (ALIASING)=0
[plane test] RESULT: FAIL -- aliasing still present
```

Predicted exactly: it memcmp'd all 2176 bytes of an `0xAA` page and tripped on
the die's parity at 2112. `cross_contaminated` was 0 the whole time — there was
never any aliasing here; the plane fix from 2026-07-26 has been sound. The test
has simply been crying wolf ever since. Now compares `usable_bytes_per_page` and
reports `PASS -- plane fix confirmed, no aliasing`.

## R2 — T2/T2b: defect A confirmed, and the boundary is 2112, not 2048

Same page, same pattern, ECC on vs ECC off:

| region | ECC on | ECC off |
|---|---|---|
| main 0..2047 | 2048 bytes match | 2048 match |
| user spare 2048..2111 | **64 match** | 64 match |
| parity 2112..2175 | **0 match, 64 other** | **64 match** |
| first mismatch | byte 2112 | none — all 2176 round-tripped |

With ECC disabled the whole page round-trips, so nothing about the driver's
addressing or transfer is wrong: the die owns 2112..2175 *because* ECC is on,
and nothing else. **Usable payload is 2112 bytes, not 2048 and not 2176.**

The user spare (2048..2111) is fully usable, so the write-up's earlier estimate
of ~5.9% loss was wrong twice over — see §R6.

## R3 — T4: defect B confirmed, on every boot tested

```
[T4] resume page @207100800: *** LIVE DATA — next write double-programs it ***
     first16: 02 00 0e 00 00 00 76 fb 85 fd 2d 06 e2 ff e8 ff
```

That is a live `IMU_FIFO` record (type 2, length 14) sitting at the offset
metadata recovery had just handed back. The next page flush programs straight
over it. Reproduced on three separate boots.

## R4 — T5: the rewind is 35,129 pages, not one

The recovered offset was page 95,176. The first genuinely erased page was
**130,305**:

```
[T5] 0 live pages from the resume offset; first erased page 130305
```

**76.4 MB of already-written log sat above the resume point.** Every boot
restarted the log 35k pages back and re-programmed its way forward until the
next reboot. That is the size of the damage window, and it is why 4.7% of pages
in the dump are corrupt rather than the ~1 page per boot the checkpoint interval
would predict.

Host-side corroboration on the same dump, using anchor ticks going backwards as
the boot marker (`debug/nand_page_audit.py` companion analysis):

- 155 boot markers, 4441 corrupt pages;
- **every one of the 155 boot-marker pages is itself corrupt** (distance 0);
- corrupt pages cluster after boots — median distance 66 pages, max 528.

## R5 — [OPEN, NOT FIXED] metadata stops advancing, and only warns

The rewind exists because metadata simply stopped being updated. The scan at
boot:

```
Block 2040: valid page 0, seq=94720      Block 2044: valid page 0, seq=94976
Block 2041: valid page 0, seq=94784      Block 2045: valid page 0, seq=95040
Block 2042: valid page 0, seq=94848      Block 2046: valid page 0, seq=95104
Block 2043: valid page 0, seq=94912      Block 2047: valid page 0, seq=95168
Block 2047 page 1/2/3: valid seq=95169/95170/95171
```

A tidy ladder 64 apart — one clean rotation cycle ending around seq 95,174 —
while the log itself had run on to page 130,305. Since a checkpoint fires every
100 records (~1 page), seq should have tracked the log to ~130,300. It stopped
~35k pages early.

`nvs_log_record()` treats a failed metadata write as non-fatal:

```c
LOG_WRN("Failed to update metadata (non-fatal): %d", ret);
```

so tens of thousands of consecutive failures are indistinguishable from a quiet
log. **This is the actual root cause of the 76 MB rewind**, and it is still
open — the append-point fix below makes it harmless (no corruption either way)
but the metadata is still not tracking the log, so recovery still cannot resume
where a session left off. Worth its own session; start by making that warning
count failures and escalate after N consecutive ones.

One datapoint on when it starts: after the chip erase, metadata tracks the log
exactly — `write_addr=765952` (page 352) with `meta_seq=349`. So the stall is not
present from page 0; it sets in later, after enough of the 512-page META
rotation cycles. That makes a long soak the way to catch it, and it lines up
with the still-open `meta_seq=64` even/odd rotation question elsewhere in
`nvs_notes.md`.

## R6 — correction to the pre-bench analysis

Two claims in the original write-up were wrong, and the bench run says so:

1. **"~5.9% of records are lost."** No. The boundary is 2112, so only 64 bytes
   per page (2.9%) are chip-owned — and in practice nothing is lost today,
   because the 100-record checkpoint flushes every page at ~2000 bytes, well
   below 2112. The console shows it on every flush: `Flushing page buffer: 2000
   bytes`. Defect A is real but **latent**; it would bite the moment the
   checkpoint interval or the record mix changed.
2. **"No page's record stream reaches past 2048, therefore the tail is lost."**
   The premise was right and the inference was wrong — pages stop at ~2000
   bytes because of the checkpoint cadence, not because the tail is discarded.

Related observation, not a defect: flushing at 100 records wastes the ~176 bytes
left in each page. About 8% of NAND capacity, in exchange for recovery
granularity.

## R7 — T6: the die knew all along

Both pages the host decoder called corrupt, re-read on-target with ECC status
finally wired up:

```
[T6] page 81638: status=0x20 eccs=0x2 first bad len @220
[T6] page 94658: status=0x20 eccs=0x2 first bad len @0
```

`eccs=0x2` is `ECC_STATUS_NOT_OK` — **uncorrectable**. The corruption offsets
match the host-side decode of the same pages byte for byte, so it is on flash,
not a dump-transport artifact. The die has been flagging these reads as
uncorrectable all along and the driver never looked. Now it counts them and logs
the first few (`mt29f_ecc_event_count()`, `mt29f_last_read_status()`).

---

# What was changed

All on `dev`, uncommitted, in the production source (not behind the probe flag):

- **`mt29f_nand.h` / `mt29f_nand.c`** — `mt29f_cfg_t` gains
  `usable_bytes_per_page = 2112` alongside `bytes_per_page = 2176`, with the
  reason written down at the definition. Address and transfer with the first,
  never put payload past the second.
- **`nvs.c`** — the record-fit check, the record-size check, and the
  `nvs_dump()` page walk all use `usable_bytes_per_page`. Padding still runs to
  2176 so the die programs parity onto erased bits.
- **`nvs.c`** — `nvs_find_append_point()`: after metadata recovery, binary-search
  forward for the first erased page and resume there. The log is append-only, so
  "written" is monotonic in the address and the boundary is a binary search —
  ~17 page reads instead of the 35,129 a linear scan would have needed here. Any
  read failure returns the recovered offset unchanged, so a flaky bus degrades to
  the old behaviour.
- **`mt29f_nand.c`** — every page read now samples STATUS and decodes ECCS,
  counting events and logging the first few. Deliberately does **not** fail the
  read: erased pages are read by design (metadata scan, `nvs_dump()`) and it is
  not yet established what this die reports for one.
- **`nvs_bringup.c`** — the T1..T6 probe behind `NVS_PAGE_PROBE`, plus the plane
  test's comparison length fixed (§R1) and its verdict mirrored to RAM.
- **`debug/no_bt.conf`** — see the bench note below.

# Verification

Same probe, after the fixes, on the same chip:

```
[plane test] even first mismatch at byte 2176
[plane test] RESULT: PASS -- plane fix confirmed, no aliasing
[T4] resume page @283543680: ERASED (safe)  first16: ff ff ff ff ...
[T5] 0 live pages from the resume offset; first erased page 130305
```

Was `LIVE DATA` and a false `FAIL` on every previous boot. The chip was then
erased (`CMD_ERASE` over the USB protocol) and the pipeline restarted clean.

# Bench state at handoff

- SN3 is running `build_n33_nobt` (production sources + `debug/no_bt.conf`,
  `NVS_PAGE_PROBE 0`), logging cleanly at 100 Hz from a freshly erased chip.
- The NAND was erased via `CMD_ERASE`. The 206 MB pre-fix dump is still on disk
  at `wwd_gui_api/data/flash_dumps/DUMP_20260826220916_first_running_activity.bin`;
  the two post-fix validation dumps are in this session's scratchpad only.
- Bench PS is not connected; the board is USB-powered.

# Bench notes

- **SN3 hangs at boot with Bluetooth enabled.** MPSL initialises at
  `PRE_KERNEL_1` and blocks forever if the LF clock source never starts. SN3's
  RV-3028 is silent again (`LFCLKSRC` = external/bypass, `LFCLKSTAT` = running on
  the RC), so the board hangs in `mpsl_init()` before USB or any application code
  runs — no console, no probe, nothing. Zephyr's own clock setup degrades to the
  RC; MPSL does not. `debug/no_bt.conf` (`CONFIG_BT=n`) was added to get past it;
  everything above was measured on that build. **The RV-3028 needs a USB power
  cycle** (see project_sn3_rv3028_silent) — it could not be done from here, the
  board is USB-powered and relay ch3 is off-limits.
- `debug/gdb_query.sh` issues `monitor reset halt`, so every GDB query restarts
  the target. That silently cost several probe runs before it was noticed —
  each one also being another double-program of the resume page. Use
  `nrfjprog --memrd` on a RAM mirror to observe a running board.
- The CDC console only exists for a short window after enumeration; a host
  reader has to reconnect within about a second of the port reappearing. The
  probe therefore mirrors every result into `probe_results` in RAM
  (see project_swd_ram_mirror_diagnostics).
