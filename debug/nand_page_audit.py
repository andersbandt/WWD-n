#!/usr/bin/env python3
"""Audit a raw WWD-n flash dump for the two NAND page defects found 2026-08-26.

See src/memory/nand_page_defects.md for the full write-up. Run against any
DUMP_*.bin produced by the GUI's USB COMM tab:

    python3 debug/nand_page_audit.py /path/to/DUMP_*.bin [stride]

`stride` samples every Nth page (default 7) so a 200 MB dump audits in seconds.

Reports, over the sampled *written* pages:
  1. where the record stream ends       -> should reach ~2160 if the full 2176 B
                                           page were really usable; it stops at
                                           <2048, which is defect A (page tail
                                           discarded by the chip's ECC-managed
                                           spare).
  2. spare-region occupancy             -> bytes 2112..2175 are non-0xFF on every
                                           written page even though the firmware
                                           pads 0xFF: that is chip-written ECC
                                           parity, not our data.
  3. unparseable pages                  -> defect B (page programmed twice
                                           without an erase; NAND programming is
                                           AND, so bits only ever clear).
  4. bit-subset test on IMU_FIFO length -> the discriminator between defect B
                                           (corrupt lengths are bit-subsets of
                                           the true 0x000e) and random bit rot
                                           or a torn/partial write (which would
                                           not be).
"""
import collections
import os
import struct
import sys

PAGE_SIZE = 2176
MAIN_BYTES = 2048          # ECC-protected main area (4 x 512 B sectors)
USER_SPARE = (2048, 2112)  # chip-managed once ECC_EN is set
PARITY = (2112, 2176)      # 4 x 16 B ECC parity, chip-written
HDR = struct.Struct("<HHH")
MAX_RECORD_TYPE = 8        # enum record_type in src/memory/nvs.h
IMU_FIFO_LEN = 0x000E


def walk_page(page):
    """Mirror dump_decoder.decode_dump()'s per-page walk. Returns (ok, end_offset)."""
    off = 0
    while off + HDR.size <= PAGE_SIZE:
        if page[off] == 0xFF:
            return True, off
        rtype, length, _dt = HDR.unpack_from(page, off)
        off += HDR.size
        if off + length > PAGE_SIZE or rtype > MAX_RECORD_TYPE:
            return False, off
        off += length
    return True, off


def main(path, stride):
    total = os.path.getsize(path) // PAGE_SIZE
    ends = collections.Counter()
    parity_first = collections.Counter()
    ones_clean, ones_bad = [], []
    subset = notsub = 0
    written = clean = bad = 0

    with open(path, "rb") as fh:
        for idx in range(0, total, stride):
            fh.seek(idx * PAGE_SIZE)
            page = fh.read(PAGE_SIZE)
            if len(page) < PAGE_SIZE or page[0] == 0xFF:
                continue
            written += 1

            spare = page[USER_SPARE[0]:PARITY[1]]
            nonff = [i + USER_SPARE[0] for i, b in enumerate(spare) if b != 0xFF]
            if nonff:
                parity_first[nonff[0]] += 1

            parity_ones = sum(bin(b).count("1") for b in page[PARITY[0]:PARITY[1]])

            ok, end = walk_page(page)
            if ok:
                clean += 1
                ends[end // 64 * 64] += 1
                ones_clean.append(parity_ones)
            else:
                bad += 1
                ones_bad.append(parity_ones)
                # Bit-subset test on the 20 B IMU_FIFO grid.
                for off in range(0, MAIN_BYTES - 8, 20):
                    length = page[off + 2] | (page[off + 3] << 8)
                    if length == IMU_FIFO_LEN:
                        continue
                    if length & ~IMU_FIFO_LEN:
                        notsub += 1
                    else:
                        subset += 1

    mean = lambda xs: sum(xs) / len(xs) if xs else float("nan")
    print(f"{path}\n  {total} pages, sampled every {stride} -> {written} written")
    print(f"  parseable: {clean}   unparseable: {bad}  ({100.0 * bad / max(1, written):.1f}%)")
    print("\n  [A] record stream end offset (parseable pages, 64 B buckets):")
    for k in sorted(ends):
        print(f"        {k:5d}..{k + 63:<5d} {ends[k]}")
    print(f"        -> nothing past {MAIN_BYTES} means the page tail never survives")
    print("\n  [A] first non-0xFF byte in the spare region:")
    for k, v in parity_first.most_common(5):
        print(f"        offset {k}: {v} pages")
    print(f"        -> {PARITY[0]} on every page = chip-written ECC parity")
    print("\n  [B] ECC parity 1-bits (of 512), parseable vs unparseable pages:")
    print(f"        parseable {mean(ones_clean):.1f}   unparseable {mean(ones_bad):.1f}")
    print("        -> fewer 1-bits on bad pages is consistent with a second program pass (AND)")
    print("\n  [B] corrupt IMU_FIFO length fields that are bit-subsets of 0x000e:")
    print(f"        subsets {subset}   non-subsets {notsub}"
          f"   ({100.0 * subset / max(1, subset + notsub):.1f}% subsets)")
    print("        -> bits only ever clear: re-program, not bit rot and not a torn write")


if __name__ == "__main__":
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    main(sys.argv[1], int(sys.argv[2]) if len(sys.argv) > 2 else 7)
