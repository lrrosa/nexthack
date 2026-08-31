#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Leonardo Roman da Rosa
#
# bankpack.py - measure what each module costs in its bank, and compute where
# the modules WOULD go if a packer chose instead of a person.
#
#   python tools/bankpack.py measure [next|zx128]   per-module banked footprint
#   python tools/bankpack.py report  [next|zx128]   the current packing + slack
#   python tools/bankpack.py plan    [next|zx128]   fewest moves that fit
#   python tools/bankpack.py plan --consolidate     pack into fewest banks
#   python tools/bankpack.py plan --grow nexthack=2000    what if it grew?
#
# Why this exists: banks.json (see the bank-manifest commit) made moving a module
# between banks one edit, but WHICH bank is still chosen by hand, one full bank
# at a time, under pressure. That is how the 128K ended up with two banks at 26
# and 42 free bytes while two others held 21 KB between them. Placement is a
# bin-packing problem; this measures it rather than arguing about it.
#
# Three things keep the numbers honest, and any change here must keep them:
#   1. Sizes are READ FROM THE BUILD, never estimated: z88dk-z80nm reports each
#      .o's per-section byte count, so a module's footprint is exactly what the
#      linker will place. Build first; a missing .o makes the tool refuse rather
#      than report a stale or partial answer.
#   2. A module's size does NOT depend on which bank it lands in -- every
#      cross-module call goes through the __banked trampoline either way, so no
#      instruction changes length. That invariant is what makes offline packing
#      valid, and it was confirmed by moving sfx between two banks: 143 bytes
#      left one and 143 arrived in the other.
#   3. The colocate groups in banks.json are packed as ONE indivisible unit. A
#      plan that split one would compile, link, pass every size check and then
#      read garbage at runtime.
#
# It only ever READS banks.json. Applying a plan is a separate, deliberate step:
# on the 128K a bank left empty must also leave tools/mktap128.py's BANKS list,
# or the tape loader tries to load a block the linker never emitted.

import itertools
import json
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
BANK_SIZE = 16384

# Where each target's objects land, and the z80nm that can read them.
OBJDIR = {"next": "src", "zx128": "obj-zx128"}
Z80NM = os.path.join(ROOT, "..", "z88dk", "bin", "z88dk-z80nm.exe")

# Sections the compiler always emits; they are the RESIDENT half, not banked
# code, so they never take part in packing.
RESIDENT_SECTIONS = {
    "", "IGNORE", "code_compiler", "rodata_compiler", "data_compiler",
    "bss_compiler", "code_crt_init", "code_home", "code_driver", "bss_driver",
}

SEC_RE = re.compile(r'^\s*Section\s+"?([A-Za-z0-9_]*)"?:\s+(\d+)\s+bytes\s*$')


def die(msg):
    sys.exit("bankpack: " + msg)


def manifest():
    with open(os.path.join(ROOT, "banks.json")) as f:
        return json.load(f)


def modules_of(man, target):
    """The target's declared modules (metadata keys start with '_')."""
    return [k for k in man[target] if not k.startswith("_")]


def sections_of(obj):
    """{section: bytes} for one object file, banked sections only."""
    if not os.path.exists(obj):
        die("%s is missing -- build the target first; this tool reports what the\n"
            "          build actually produced, never an estimate." % obj)
    try:
        out = subprocess.run([Z80NM, "-a", obj], capture_output=True, text=True).stdout
    except FileNotFoundError:
        die("cannot run %s -- the z88dk nightly supplies it." % Z80NM)
    got = {}
    for line in out.splitlines():
        m = SEC_RE.match(line)
        if m and m.group(1) not in RESIDENT_SECTIONS and int(m.group(2)) > 0:
            got[m.group(1)] = int(m.group(2))
    return got


def measure(man, target):
    """module -> (bytes, [sections it occupies]). Resident modules are 0/[]."""
    d = OBJDIR[target]
    out = {}
    for mod in modules_of(man, target):
        sec = sections_of(os.path.join(ROOT, d, mod + ".o"))
        out[mod] = (sum(sec.values()), sorted(sec))
    return out


def units(man, target, size):
    """Pack in indivisible units: a colocate group is one unit, everything else
    is its own. Returns [(name, [modules], bytes, current_bank, pinned)]."""
    mods = modules_of(man, target)
    tm = man[target]
    pool = tm["_pool"]

    # Union-find over modules. Groups that SHARE a module are one unit: on the
    # Next, nexthack+classes+spells and nexthack+titlepal+victorypal overlap, so
    # all five must land in the same bank -- show_layer2 streams the palettes
    # while its own bank is mapped. Treating the two groups separately would
    # produce a plan that links, boots, and scrambles the title screen.
    parent = {}

    def find(x):
        parent.setdefault(x, x)
        while parent[x] != x:
            parent[x] = parent[parent[x]]
            x = parent[x]
        return x

    def union(a, b):
        ra, rb = find(a), find(b)
        if ra != rb:
            parent[rb] = ra

    for g in man["colocate"]:
        members = [m for m in g["modules"] if m in mods and size[m][0] > 0]
        for m in members[1:]:
            union(members[0], m)

    merged = {}
    for m in parent:
        merged.setdefault(find(m), []).append(m)
    merged = {r: ms for r, ms in merged.items() if len(ms) > 1}

    grouped = {m for ms in merged.values() for m in ms}
    out = []
    for g, ms in sorted(merged.items()):
        ms = sorted(ms)
        banks = {b for m in ms for b in size[m][1]}
        out.append(("+".join(ms), ms, sum(size[m][0] for m in ms),
                    sorted(banks)[0] if banks else None,
                    not all(b in pool for b in banks)))
    for m in mods:
        if m in grouped or size[m][0] == 0:
            continue
        banks = size[m][1]
        out.append((m, [m], size[m][0], banks[0] if banks else None,
                    not all(b in pool for b in banks)))
    return out


def current(man, target, us):
    """bank -> [(unit, bytes)] as the build has it today."""
    occ = {b: [] for b in man[target]["_pool"]}
    for name, _, sz, bank, pinned in us:
        if not pinned:
            occ.setdefault(bank, []).append((name, sz))
    return occ


def ffd(pool, us):
    """First-fit-decreasing over the pool, biggest unit first. Banks are filled
    in pool order, so the slack collects at the end where it is usable."""
    occ = {b: [] for b in pool}
    left = dict.fromkeys(pool, BANK_SIZE)
    unplaced = []
    for name, _, sz, _, pinned in sorted(us, key=lambda u: -u[2]):
        if pinned:
            continue
        for b in pool:
            if left[b] >= sz:
                occ[b].append((name, sz))
                left[b] -= sz
                break
        else:
            unplaced.append((name, sz))
    return occ, unplaced


def show(occ, pool, title):
    print("  %s" % title)
    for b in pool:
        items = occ.get(b, [])
        used = sum(s for _, s in items)
        flag = ""
        if not items:
            flag = "   <== EMPTY: a whole spare bank"
        elif BANK_SIZE - used < 128:
            flag = "   <== effectively FULL"
        print("    %-14s %6d / %d B  %6d free%s"
              % (b, used, BANK_SIZE, BANK_SIZE - used, flag))
        for n, s in sorted(items, key=lambda x: -x[1]):
            print("        %-38s %6d" % (n, s))


def cmd_measure(target):
    man = manifest()
    size = measure(man, target)
    print("== %s: banked footprint per module (from the .o) ==" % target)
    tot = 0
    for m, (sz, secs) in sorted(size.items(), key=lambda x: -x[1][0]):
        if sz == 0:
            continue
        tot += sz
        print("    %-16s %6d   %s" % (m, sz, ",".join(secs)))
    res = [m for m, (sz, _) in size.items() if sz == 0]
    print("    %-16s %6d" % ("TOTAL banked", tot))
    print("    resident: %s" % ", ".join(sorted(res)))


def cmd_report(target):
    man = manifest()
    size = measure(man, target)
    us = units(man, target, size)
    pool = man[target]["_pool"]
    print("== %s: current packing ==" % target)
    show(current(man, target, us), pool, "as banks.json has it")
    pins = [(n, s) for n, _, s, b, p in us if p]
    if pins:
        print("  pinned outside the pool (never packed):")
        for n, s in sorted(pins):
            print("    %-38s %6d" % (n, s))


def repair(pool, us, load, grown):
    """Fewest unit moves that bring every bank back under 16 KB.

    Minimising MOVES, not banks used: each move costs a recompile and a fresh
    round of emulator verification, and a plan that shuffles everything to win
    2 KB is a bad trade. Returns (moves, ok) with moves = [(unit, from, to)].
    """
    over = [b for b in pool if load[b] > BANK_SIZE]
    if not over:
        return [], True
    movable = [(n, b, grown[n]) for n, _, _, b, p in us if not p and b in over]
    for k in range(1, len(movable) + 1):
        for combo in itertools.combinations(movable, k):
            left = dict(load)
            for n, b, sz in combo:
                left[b] -= sz
            if any(left[b] > BANK_SIZE for b in pool):
                continue
            moves, ok = [], True
            for n, b, sz in sorted(combo, key=lambda c: -c[2]):
                for dst in pool:
                    if dst != b and BANK_SIZE - left[dst] >= sz:
                        left[dst] += sz
                        moves.append((n, b, dst))
                        break
                else:
                    ok = False
                    break
            if ok:
                return moves, True
    return [], False


def cmd_plan(target, consolidate=False, grow=None):
    man = manifest()
    size = measure(man, target)
    us = units(man, target, size)
    pool = man[target]["_pool"]
    cur = current(man, target, us)

    # Growth is asked per MODULE but lands on the whole unit it belongs to.
    grown = {n: sz for n, _, sz, _, p in us if not p}
    for mod, extra in (grow or {}).items():
        for n, mods, _, _, p in us:
            if mod in mods and not p:
                grown[n] += extra
                break
        else:
            die("'%s' is not a banked module of %s." % (mod, target))
    load = {b: sum(grown[n] for n, _ in cur.get(b, [])) for b in pool}

    print("== %s: %s ==" % (target, "consolidation plan" if consolidate else
                            "placement plan"))
    if grow:
        print("  hypothetical growth: %s"
              % ", ".join("%s +%d B" % kv for kv in sorted(grow.items())))
    show(cur, pool, "CURRENT" if not grow else "CURRENT (with the growth applied)")

    # The binding constraint is the biggest INDIVISIBLE unit, not the total.
    big = max((grown[n], n, mods) for n, mods, _, _, p in us if not p)
    slack = BANK_SIZE - big[0]
    print("\n  Largest indivisible unit: %s = %d B, %d B %s a full bank."
          % (big[1], big[0], abs(slack), "under" if slack >= 0 else "OVER"))
    if slack < 1024:
        print("  No packing can give it room: it must share a bank with nothing,\n"
              "  and it is already alone. Growing it means splitting the module or\n"
              "  breaking its colocate group (reach the data through a __banked\n"
              "  accessor instead of a pointer), not moving it.")

    if consolidate:
        new, unplaced = ffd(pool, us)
        print()
        show(new, pool, "CONSOLIDATED (fewest banks -- maximises ONE free block)")
        if unplaced:
            print("\n  DOES NOT FIT:")
            for n, s in unplaced:
                print("    %-38s %6d" % (n, s))
        where = {n: b for b, items in new.items() for n, _ in items}
        moves = [(n, b, where[n]) for n, _, _, b, p in us
                 if not p and where.get(n) and where[n] != b]
    else:
        moves, ok = repair(pool, us, load, grown)
        if not moves and ok:
            free = sorted((BANK_SIZE - load[b], b) for b in pool)
            print("\n  Everything fits. No move needed.")
            print("  Slack: %s" % ", ".join("%s %d B" % (b, f) for f, b in
                                            reversed(free)))
            return
        if not ok:
            print("\n  NO PLACEMENT FITS. This is a real budget wall: the modules\n"
                  "  total more than the %d banks hold, or an indivisible unit has\n"
                  "  nowhere to go. Shrink, split, or claim another bank." % len(pool))
            return

    print("\n  moves: %d unit(s)" % len(moves))
    for n, a, b in sorted(moves):
        print("    %-38s %-14s -> %s" % (n, a, b))
    mods = sorted(m for n, _, _ in moves for u in us if u[0] == n for m in u[1])
    print("  = %d module(s) to recompile: %s" % (len(mods), ", ".join(mods)))

    after = {b: load[b] for b in pool}
    for n, a, b in moves:
        after[a] -= grown[n]
        after[b] += grown[n]
    print("\n  largest free block: %d B now -> %d B after"
          % (max(BANK_SIZE - load[b] for b in pool),
             max(BANK_SIZE - after[b] for b in pool)))


USAGE = ("usage: bankpack.py [measure|report|plan] [next|zx128]\n"
         "                  plan --consolidate      pack into the fewest banks\n"
         "                  plan --grow mod=BYTES   what if this module grew?")


def main():
    args = sys.argv[1:]
    cmd = args[0] if args and not args[0].startswith("-") else "report"
    if cmd not in ("measure", "report", "plan"):
        die(USAGE)
    consolidate = "--consolidate" in args
    grow = {}
    for i, a in enumerate(args):
        if a == "--grow" and i + 1 < len(args):
            k, _, v = args[i + 1].partition("=")
            if not v.lstrip("+").isdigit():
                die("--grow wants module=BYTES, got %r" % args[i + 1])
            grow[k] = int(v)
    targets = [a for a in args[1:] if a in OBJDIR] or ["next", "zx128"]
    for i, t in enumerate(targets):
        if i:
            print()
        if cmd == "plan":
            cmd_plan(t, consolidate, grow)
        else:
            {"measure": cmd_measure, "report": cmd_report}[cmd](t)


if __name__ == "__main__":
    main()
