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
#   python tools/bankpack.py plan zx128 --free BANK_4    give a bank headroom
#   python tools/bankpack.py apply zx128 --grow monster_ai=2000   write it
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
# `apply` is the only subcommand that writes, and it writes ONLY banks.json --
# surgically, so the hand-written "why" text survives. Everything downstream
# already follows the manifest: mktap128.py derives the tape's bank list from
# it, so a repack that empties a bank drops that block by itself.
#
# After an apply, rebuild AND re-verify in the emulator: every address in the
# touched banks moves, so a latent bank-discipline bug surfaces there.

import collections
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

# A packing unit: one module, or a whole colocate group. The first five fields
# are unpacked positionally all over this file; `hot` rides along for the
# contended-RAM preference below.
Unit = collections.namedtuple("Unit", "name mods size bank pinned hot")

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
    hot = lambda ms: any(tm[m].get("hot") for m in ms)
    out = []
    for g, ms in sorted(merged.items()):
        ms = sorted(ms)
        banks = {b for m in ms for b in size[m][1]}
        out.append(Unit("+".join(ms), ms, sum(size[m][0] for m in ms),
                        sorted(banks)[0] if banks else None,
                        not all(b in pool for b in banks), hot(ms)))
    for m in mods:
        if m in grouped or size[m][0] == 0:
            continue
        banks = size[m][1]
        out.append(Unit(m, [m], size[m][0], banks[0] if banks else None,
                        not all(b in pool for b in banks), hot([m])))
    return out


def current(man, target, us):
    """bank -> [(unit, bytes)] as the build has it today."""
    occ = {b: [] for b in man[target]["_pool"]}
    for name, _, sz, bank, pinned, _h in us:
        if not pinned:
            occ.setdefault(bank, []).append((name, sz))
    return occ


def ffd(pool, us):
    """First-fit-decreasing over the pool, biggest unit first. Banks are filled
    in pool order, so the slack collects at the end where it is usable."""
    occ = {b: [] for b in pool}
    left = dict.fromkeys(pool, BANK_SIZE)
    unplaced = []
    for name, _, sz, _, pinned, _h in sorted(us, key=lambda u: -u[2]):
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
    pins = [(n, s) for n, _, s, b, p, _h in us if p]
    if pins:
        print("  pinned outside the pool (never packed):")
        for n, s in sorted(pins):
            print("    %-38s %6d" % (n, s))


def repair(pool, us, load, grown, contended=()):
    """Fewest unit moves that bring every bank back under 16 KB.

    Minimising MOVES, not banks used: each move costs a recompile and a fresh
    round of emulator verification, and a plan that shuffles everything to win
    2 KB is a bad trade. Returns (moves, ok) with moves = [(unit, from, to)].
    """
    over = [b for b in pool if load[b] > BANK_SIZE]
    if not over:
        return [], True
    movable = [(n, b, grown[n], h) for n, _, _, b, p, h in us if not p and b in over]
    for k in range(1, len(movable) + 1):
        for combo in itertools.combinations(movable, k):
            left = dict(load)
            for n, b, sz, _hot in combo:
                left[b] -= sz
            if any(left[b] > BANK_SIZE for b in pool):
                continue
            moves, ok = [], True
            for n, b, sz, hot in sorted(combo, key=lambda c: -c[2]):
                # A hot unit (per-turn code) tries the uncontended banks first:
                # on the 128K the ULA steals cycles from banks 1/3/5/7, so the
                # chase AI landing there is a real slowdown. Preference, not a
                # rule -- if only a contended bank has room, it still goes.
                order = pool if not hot else ([b2 for b2 in pool if b2 not in contended] +
                                              [b2 for b2 in pool if b2 in contended])
                for dst in order:
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


def compute(target, consolidate=False, grow=None, free_bank=None):
    """Everything both `plan` and `apply` need: the units, where they are now,
    and the moves. Returns a dict; `moves` is [] when nothing needs to move."""
    man = manifest()
    size = measure(man, target)
    us = units(man, target, size)
    tm = man[target]
    pool = tm["_pool"]
    cur = current(man, target, us)

    # Growth is asked per MODULE but lands on the whole unit it belongs to.
    grown = {n: sz for n, _, sz, _, p, _h in us if not p}
    for mod, extra in (grow or {}).items():
        for n, mods, _, _, p, _h in us:
            if mod in mods and not p:
                grown[n] += extra
                break
        else:
            die("'%s' is not a banked module of %s." % (mod, target))
    load = {b: sum(grown[n] for n, _ in cur.get(b, [])) for b in pool}

    if free_bank:
        if free_bank not in pool:
            die("'%s' is not a bank of %s (pool: %s)."
                % (free_bank, target, ", ".join(pool)))
        new, unplaced = None, []
        moves, _freed = relieve(pool, us, load, grown,
                                tm.get("_contended", []), free_bank)
        ok = True
    elif consolidate:
        new, unplaced = ffd(pool, us)
        where = {n: b for b, items in new.items() for n, _ in items}
        moves = [(n, b, where[n]) for n, _, _, b, p, _h in us
                 if not p and where.get(n) and where[n] != b]
        ok = not unplaced
    else:
        new, unplaced = None, []
        moves, ok = repair(pool, us, load, grown, tm.get("_contended", []))
    return dict(man=man, us=us, pool=pool, cur=cur, grown=grown, load=load,
                moves=moves, ok=ok, new=new, unplaced=unplaced)


def relieve(pool, us, load, grown, contended, bank):
    """Fewest moves that get the most out of ONE named bank.

    `plan` answers "what is the minimum that makes it fit", which is silent
    while a bank still fits at 627 free bytes. This answers the question that
    actually gets asked -- "free BANK_4" -- by emptying as much of it as a
    single small set of moves can, without breaking anything else.
    """
    cands = [(n, b, grown[n], h) for n, _, _, b, p, h in us if not p and b == bank]
    if not cands:
        return [], 0
    best = None
    for k in range(1, len(cands) + 1):
        for combo in itertools.combinations(cands, k):
            left = dict(load)
            for n, b, sz, _h in combo:
                left[b] -= sz
            moves, ok = [], True
            for n, b, sz, hot in sorted(combo, key=lambda c: -c[2]):
                order = pool if not hot else (
                    [x for x in pool if x not in contended] +
                    [x for x in pool if x in contended])
                for dst in order:
                    if dst != b and BANK_SIZE - left[dst] >= sz:
                        left[dst] += sz
                        moves.append((n, b, dst))
                        break
                else:
                    ok = False
                    break
            if not ok:
                continue
            freed = sum(c[2] for c in combo)
            if best is None or freed > best[1]:
                best = (moves, freed)
        if best:
            break          # minimal k wins; among those, the most freed
    return best if best else ([], 0)


def cmd_plan(target, consolidate=False, grow=None, free_bank=None):
    c = compute(target, consolidate, grow, free_bank)
    man, us, pool, cur, grown, load = (c["man"], c["us"], c["pool"], c["cur"],
                                       c["grown"], c["load"])

    print("== %s: %s ==" % (target, "consolidation plan" if consolidate else
                            "placement plan"))
    if grow:
        print("  hypothetical growth: %s"
              % ", ".join("%s +%d B" % kv for kv in sorted(grow.items())))
    show(cur, pool, "CURRENT" if not grow else "CURRENT (with the growth applied)")

    # The binding constraint is the biggest INDIVISIBLE unit, not the total.
    big = max((grown[n], n, mods) for n, mods, _, _, p, _h in us if not p)
    slack = BANK_SIZE - big[0]
    print("\n  Largest indivisible unit: %s = %d B, %d B %s a full bank."
          % (big[1], big[0], abs(slack), "under" if slack >= 0 else "OVER"))
    if slack < 1024:
        print("  No packing can give it room: it must share a bank with nothing,\n"
              "  and it is already alone. Growing it means splitting the module or\n"
              "  breaking its colocate group (reach the data through a __banked\n"
              "  accessor instead of a pointer), not moving it.")

    moves = c["moves"]
    if consolidate:
        print()
        show(c["new"], pool, "CONSOLIDATED (fewest banks -- maximises ONE free block)")
        if c["unplaced"]:
            print("\n  DOES NOT FIT:")
            for n, s in c["unplaced"]:
                print("    %-38s %6d" % (n, s))
    elif free_bank:
        if not moves:
            print("\n  Nothing in %s can move: it holds only pinned or "
                  "unplaceable units." % free_bank)
            return
    else:
        ok = c["ok"]
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
    if free_bank:
        print("\n  %s: %d B free now -> %d B after"
              % (free_bank, BANK_SIZE - load[free_bank],
                 BANK_SIZE - after[free_bank]))
    print("\n  largest free block: %d B now -> %d B after"
          % (max(BANK_SIZE - load[b] for b in pool),
             max(BANK_SIZE - after[b] for b in pool)))


def rewrite_manifest(target, changes):
    """Point each moved module at its new bank, IN PLACE.

    A surgical line edit, not json.dump: re-serialising would reflow the whole
    file and throw away the key order, the alignment and the per-entry "why"
    text, which is the part a human actually reads. Each entry is one line, so
    only the bank names on those lines change.
    """
    # newline="" both ways: read and write the file's own line endings back
    # untouched, so the diff is the banks that moved and nothing else.
    path = os.path.join(ROOT, "banks.json")
    with open(path, newline="") as f:
        text = f.read()

    start = text.index('  "%s": {' % target)
    end = text.index("\n  },", start)
    head, body, tail = text[:start], text[start:end], text[end:]

    for mod, newbank in sorted(changes.items()):
        pat = re.compile(r'^(\s*"%s":\s*\{.*)$' % re.escape(mod), re.M)
        m = pat.search(body)
        if not m:
            die("no '%s' entry in the %s section of banks.json." % (mod, target))
        line = m.group(1)
        fixed = re.sub(r'("(?:code|const)":\s*")[^"]+(")',
                       lambda g: g.group(1) + newbank + g.group(2), line)
        if fixed == line:
            die("'%s' has no code/const key to move." % mod)
        body = body[:m.start(1)] + fixed + body[m.end(1):]

    with open(path, "w", newline="") as f:
        f.write(head + body + tail)


def cmd_apply(target, consolidate=False, grow=None, free_bank=None):
    c = compute(target, consolidate, grow, free_bank)
    if not c["ok"]:
        die("no placement fits -- nothing to apply. Run 'plan' for the detail.")
    if not c["moves"]:
        print("bankpack: %s already fits; nothing to move." % target)
        return

    unit_mods = {n: mods for n, mods, _, _, _, _h in c["us"]}
    changes = {m: dst for n, _, dst in c["moves"] for m in unit_mods[n]}

    print("== %s: applying %d move(s) to banks.json ==" % (target, len(c["moves"])))
    for n, src, dst in sorted(c["moves"]):
        print("    %-38s %-14s -> %s" % (n, src, dst))
    rewrite_manifest(target, changes)

    print("\n  %d module(s) will recompile: %s"
          % (len(changes), ", ".join(sorted(changes))))
    print("  The 'why' text of the moved entries still describes the OLD bank.\n"
          "  It is prose a person wrote; fix it by hand rather than let it lie.")
    print("  Then rebuild and RE-VERIFY: every address in those banks changed, so\n"
          "  a latent bank-discipline bug surfaces here and nowhere else.")


USAGE = ("usage: bankpack.py [measure|report|plan|apply] [next|zx128]\n"
         "                  plan --consolidate      pack into the fewest banks\n"
         "                  plan --grow mod=BYTES   what if this module grew?\n"
         "                  plan --free BANK_4      empty a named bank\n"
         "                  apply ...               write the plan to banks.json")


def main():
    args = sys.argv[1:]
    cmd = args[0] if args and not args[0].startswith("-") else "report"
    if cmd not in ("measure", "report", "plan", "apply"):
        die(USAGE)
    consolidate = "--consolidate" in args
    grow = {}
    free_bank = None
    for i, a in enumerate(args):
        if a == "--free" and i + 1 < len(args):
            free_bank = args[i + 1]
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
            cmd_plan(t, consolidate, grow, free_bank)
        elif cmd == "apply":
            cmd_apply(t, consolidate, grow, free_bank)
        else:
            {"measure": cmd_measure, "report": cmd_report}[cmd](t)


if __name__ == "__main__":
    main()
