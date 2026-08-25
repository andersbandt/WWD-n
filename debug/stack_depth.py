#!/usr/bin/env python3
"""Static worst-case stack depth per thread.

Combines two things the compiler already knows but never puts together:

  * ``-fstack-usage`` writes a ``.su`` file beside every object, giving each
    function's own frame size.
  * ``objdump -d`` gives the call graph (``bl``/``blx`` targets).

Walking the graph from a thread entry point, summing frames and taking the max
at each branch, gives that thread's worst-case depth. See debug/stack_budget.md
for results, and for the (important) list of what this does NOT catch --
chiefly that indirect calls through function pointers are invisible, so results
are a guide, not a bound, and do not replace a run with stackcheck.conf.

Usage:
    python3 debug/stack_depth.py <zephyr.elf> <build-dir>
"""
import collections
import os
import re
import shutil
import subprocess
import sys

# Thread entry points and their configured stack sizes. Keep in sync with
# src/main.c and src/comm/protocol.c.
THREADS = [
    ("main",                  "main",                        4096),
    ("sensor_update_thread",  "sensor_update_thread_entry",  4096),
    ("ui_refresh_thread",     "ui_refresh_thread_entry",     4096),
    ("button_handler_thread", "button_handler_thread_entry", 4096),
    ("protocol_thread",       "protocol_thread_fn",          4096),
]

SDK = "/home/anders/ncs/toolchains/b2ecd2435d/opt/zephyr-sdk"
OBJDUMP = os.environ.get("OBJDUMP") or shutil.which("arm-zephyr-eabi-objdump") \
    or f"{SDK}/arm-zephyr-eabi/bin/arm-zephyr-eabi-objdump"


def load_frames(build_dir):
    """name -> own frame size, from every .su file under build_dir."""
    frames = {}
    for root, _dirs, files in os.walk(build_dir):
        for f in files:
            if not f.endswith(".su"):
                continue
            with open(os.path.join(root, f)) as fh:
                for line in fh:
                    parts = line.split("\t")
                    if len(parts) < 2:
                        continue
                    name = parts[0].split(":")[-1]
                    try:
                        size = int(parts[1])
                    except ValueError:
                        continue
                    frames[name] = max(frames.get(name, 0), size)
    return frames


def load_callgraph(elf):
    """caller -> {callees}, from the disassembly."""
    dis = subprocess.run([OBJDUMP, "-d", elf],
                         capture_output=True, text=True, check=True).stdout
    func_re = re.compile(r"^[0-9a-f]+ <(.+)>:$")
    call_re = re.compile(r"\b(?:bl|blx)\s+[0-9a-f]+ <([^>+]+)(?:\+0x[0-9a-f]+)?>")
    graph = collections.defaultdict(set)
    seen = set()
    cur = None
    for line in dis.splitlines():
        m = func_re.match(line)
        if m:
            cur = m.group(1)
            seen.add(cur)
            continue
        if cur:
            c = call_re.search(line)
            if c:
                graph[cur].add(c.group(1))
    return graph, seen


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    elf, build_dir = sys.argv[1], sys.argv[2]

    frames = load_frames(build_dir)
    if not frames:
        sys.exit(f"no .su files under {build_dir} -- build with "
                 f"-DEXTRA_CONF_FILE=debug/stack_usage.conf")
    graph, seen = load_callgraph(elf)

    memo, onstack = {}, set()

    def depth(fn):
        if fn in memo:
            return memo[fn]
        if fn in onstack:          # recursion -- break the cycle
            return (0, [])
        onstack.add(fn)
        best, best_path = 0, []
        for callee in graph.get(fn, ()):
            d, path = depth(callee)
            if d > best:
                best, best_path = d, path
        onstack.discard(fn)
        memo[fn] = (frames.get(fn, 0) + best, [fn] + best_path)
        return memo[fn]

    print(f"{'thread':24} {'stack':>6} {'worst case':>11} {'used':>7}  deepest chain")
    print("-" * 110)
    worst_pct = 0.0
    for name, entry, size in THREADS:
        if entry not in seen and entry not in frames:
            print(f"{name:24} {size:>6}   <entry symbol {entry!r} not found>")
            continue
        d, path = depth(entry)
        pct = 100.0 * d / size
        worst_pct = max(worst_pct, pct)
        flag = "  <-- OVER" if pct >= 100 else ("  <-- tight" if pct >= 75 else "")
        chain = " -> ".join(path[:6]) + (" ..." if len(path) > 6 else "")
        print(f"{name:24} {size:>6} {d:>9} B {pct:>6.1f}%  {chain}{flag}")

    print()
    print("Indirect calls (function pointers, Zephyr device APIs, work handlers)")
    print("are NOT in the call graph -- see debug/stack_budget.md before trusting")
    print("these as an upper bound. Confirm on hardware with stackcheck.conf.")
    return 1 if worst_pct >= 100 else 0


if __name__ == "__main__":
    sys.exit(main())
