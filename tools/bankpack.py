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
#   python tools/bankpack.py compress zx128   what packs well (a HINT, read it)
#   python tools/bankpack.py apply zx128 --grow monster_ai=2000   write it
#   python tools/bankpack.py selftest         the search vs known answers (CI)
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
sys.path.insert(0, HERE)
import lztmpl                       # the game's own packer, so the ratio is real
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
DUMP_RE = re.compile(r'^\s*C \$[0-9A-Fa-f]+: ((?:[0-9A-Fa-f]{2} ?)+)\s*$')


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


def section_bytes(obj):
    """{section: bytes} -- the CONTENT z80nm dumps, banked sections only.

    The bytes still carry unresolved relocations (address fields read 00 00),
    which makes code pack a shade better here than it truly would. Data is
    unaffected -- and data is the only thing this measurement can be spent on.
    """
    out = subprocess.run([Z80NM, "-a", obj], capture_output=True, text=True).stdout
    got, cur = {}, None
    for line in out.splitlines():
        m = SEC_RE.match(line)
        if m:
            cur = m.group(1) if m.group(1) not in RESIDENT_SECTIONS else None
            if cur:
                got.setdefault(cur, bytearray())
            continue
        m = DUMP_RE.match(line)
        if m and cur is not None:
            got[cur] += bytes.fromhex(m.group(1).replace(" ", ""))
    return {k: bytes(v) for k, v in got.items() if v}


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


def cmd_compress(target):
    """How well each module's banked bytes pack -- a HINT, not a plan.

    It answers "is there anything here worth compressing", which is the question
    that found the templates: 8400 B of grids at 10%. It cannot answer "should
    you", because that turns on something no measurement sees -- see the rule
    printed at the end.
    """
    man = manifest()
    tm = man[target]
    rows = []
    for mod in modules_of(man, target):
        secs = section_bytes(os.path.join(ROOT, OBJDIR[target], mod + ".o"))
        raw = b"".join(secs[k] for k in sorted(secs))
        if not raw:
            continue
        packed = len(lztmpl.compress(raw))
        rows.append((len(raw) - packed, mod, len(raw), packed,
                     "const" if tm[mod].get("const") else "code only"))

    print("== %s: how well each module's banked bytes pack ==" % target)
    print("  (tools/lztmpl.py -- the format the game already decompresses)")
    print("  %-16s %7s %8s %7s %8s   %s"
          % ("module", "bytes", "packed", "ratio", "would free", "section holds"))
    for saved, mod, raw, packed, kind in sorted(rows, reverse=True):
        print("  %-16s %7d %8d %6.1f%% %8d   %s"
              % (mod, raw, packed, 100.0 * packed / raw, saved, kind))

    print("\n  Spend this ONLY where the thing it unpacks into already exists and")
    print("  the data is read once. Templates qualified -- load_template was")
    print("  going to write those 1680 bytes into lvl[][] regardless, so the")
    print("  destination was free and the bank kept the difference.")
    print("  CODE never qualifies: to run from a bank it must be there expanded,")
    print("  so you would carry both forms and save nothing. A 'code only' row")
    print("  packing well is a mirage.")
    print("  Strings are the awkward middle: they need a scratch buffer and the")
    print("  work lands in the per-turn path.")


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


# How many search nodes repair() may spend before it says "not found" instead
# of answering. The real targets settle in a few thousand; the cap is there so
# an unlucky input ends in an honest "gave up" rather than an hour of CPU.
SEARCH_NODES = 2000000


class _OutOfNodes(Exception):
    pass


def _place(items, pool, contended, max_moves, budget, feasibility):
    """Depth-first: put every item [(name, size, home, hot)] -- biggest first
    -- in a bank, with at most `max_moves` of them away from home. Returns
    {name: bank} or None; raises _OutOfNodes when the budget runs dry.

    Every bank starts EMPTY and only grows, so a bank over 16 KB at any depth
    is a dead end -- which is what makes swaps cheap to find: a unit leaving a
    full bank and another arriving in its place are just two items whose
    banks are not their homes. The old search only moved units OUT of the
    banks that were over, into whatever had room, so it could not see that
    room had to be made first (issue #17).

    `feasibility` drops the move bound and the home preference, and treats
    banks with the same load as interchangeable -- for "does ANY packing
    exist", which is then a plain bin-packing proof."""
    load = dict.fromkeys(pool, 0)
    where = {}
    rest = [0] * (len(items) + 1)            # bytes still to place from i on
    for i in range(len(items) - 1, -1, -1):
        rest[i] = rest[i + 1] + items[i][1]

    def moves_needed(i):
        """A lower bound on the moves the items from i on still need: a bank
        that would overflow if all of its own stayed home must lose at least
        as many of them as it takes, biggest first, to cover the excess."""
        own = {}
        for _, sz, home, _ in items[i:]:
            own.setdefault(home, []).append(sz)     # already biggest first
        need = 0
        for home, sizes in own.items():
            over = load[home] + sum(sizes) - BANK_SIZE
            for sz in sizes:
                if over <= 0:
                    break
                over -= sz
                need += 1
        return need

    def dfs(i, used):
        budget[0] -= 1
        if budget[0] < 0:
            raise _OutOfNodes()
        if i == len(items):
            return True
        if rest[i] > sum(BANK_SIZE - v for v in load.values()):
            return False
        if not feasibility and used + moves_needed(i) > max_moves:
            return False
        name, sz, home, hot = items[i]
        if feasibility:
            order, tried = sorted(pool, key=lambda b: (b != home, load[b])), set()
        else:
            away = [b for b in pool if b != home]
            if hot:      # per-turn code: the uncontended banks first (see below)
                away = ([b for b in away if b not in contended] +
                        [b for b in away if b in contended])
            order = [home] + (away if used < max_moves else [])
        for b in order:
            if load[b] + sz > BANK_SIZE:
                continue
            if feasibility:
                if load[b] in tried:         # same load, same future: one is enough
                    continue
                tried.add(load[b])
            load[b] += sz
            where[name] = b
            if dfs(i + 1, used + (b != home)):
                return True
            load[b] -= sz
            del where[name]
        return False

    return where if dfs(0, 0) else None


def repair(pool, us, load, grown, contended=()):
    """Fewest unit moves that bring every bank back under 16 KB.

    Minimising MOVES, not banks used: each move costs a recompile and a fresh
    round of emulator verification, and a plan that shuffles everything to win
    2 KB is a bad trade. A move may also be the one that MAKES room -- a unit
    out of a bank that fits, so that a unit from an overfull bank can land
    there; the first version never tried those and declared a wall where a
    swap fitted (#17).

    Returns (moves, status): moves = [(unit, from, to)], and status is
      "ok"          the fewest moves, proven (every smaller count was tried);
      "ok-unproven" a plan that fits, but the node budget ran out before every
                    smaller one could be ruled out;
      "impossible"  proven: no placement of the units fits the banks at all;
      "unknown"     the budget ran out before either a plan or a proof.
    A hot unit (per-turn code) tries the uncontended banks first: on the 128K
    the ULA steals cycles from banks 1/3/5/7, so the chase AI landing there is
    a real slowdown. Preference, not a rule -- if only a contended bank has
    room, it still goes.
    """
    if not any(load[b] > BANK_SIZE for b in pool):
        return [], "ok"
    items = sorted(((n, grown[n], b, h) for n, _, _, b, p, h in us if not p),
                   key=lambda it: (-it[1], it[0]))
    if (sum(it[1] for it in items) > BANK_SIZE * len(pool) or
            any(it[1] > BANK_SIZE for it in items)):
        return [], "impossible"

    def as_moves(where):
        return [(n, home, where[n]) for n, _, home, _ in items if where[n] != home]

    budget = [SEARCH_NODES]
    try:
        some = _place(items, pool, contended, len(items), budget, True)
    except _OutOfNodes:
        return [], "unknown"
    if some is None:
        return [], "impossible"
    best = as_moves(some)
    try:
        for k in range(1, len(best)):
            where = _place(items, pool, contended, k, budget, False)
            if where is not None:
                return as_moves(where), "ok"
    except _OutOfNodes:
        return best, "ok-unproven"
    # nothing smaller fits; redo it at this count for the preferred banks
    try:
        where = _place(items, pool, contended, len(best), budget, False)
    except _OutOfNodes:
        where = None
    return (as_moves(where) if where is not None else best), "ok"


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

    status = "ok"
    if free_bank:
        if free_bank not in pool:
            die("'%s' is not a bank of %s (pool: %s)."
                % (free_bank, target, ", ".join(pool)))
        new, unplaced = None, []
        moves, _freed = relieve(pool, us, load, grown,
                                tm.get("_contended", []), free_bank)
    elif consolidate:
        new, unplaced = ffd(pool, us)
        where = {n: b for b, items in new.items() for n, _ in items}
        moves = [(n, b, where[n]) for n, _, _, b, p, _h in us
                 if not p and where.get(n) and where[n] != b]
        status = "ok" if not unplaced else "unknown"    # FFD is a heuristic
    else:
        new, unplaced = None, []
        moves, status = repair(pool, us, load, grown, tm.get("_contended", []))
    return dict(man=man, us=us, pool=pool, cur=cur, grown=grown, load=load,
                moves=moves, ok=status.startswith("ok"), status=status,
                new=new, unplaced=unplaced)


def relieve(pool, us, load, grown, contended, bank):
    """Fewest moves that give ONE named bank headroom -- without making another
    bank the new tight one.

    `plan` answers "what is the minimum that makes it fit", which is silent
    while a bank still fits at 627 free bytes; this answers "free BANK_4".

    Asking to free a bank means two things at once: it gains room, AND no other
    bank becomes the next one to fill. So among the plans with the fewest
    moves, this keeps the one whose TIGHTEST TOUCHED BANK -- the source and
    every destination -- ends roomiest. The first version took the first bank
    with room in pool order: asked to free the 128K's BANK_7 it moved
    nexthack_lvl into BANK_1 and left THAT at 123 B, which moves the shortage
    instead of curing it. Ranking, most important first:
      1. no hot unit in a contended bank, when any plan avoids it;
      2. the tightest touched bank, as roomy as possible;
      3. the most freed from the named bank.
    """
    cands = [(n, grown[n], h) for n, _, _, b, p, h in us if not p and b == bank]
    dests = [d for d in pool if d != bank]
    best = None
    for k in range(1, len(cands) + 1):
        for combo in itertools.combinations(cands, k):
            for assign in itertools.product(dests, repeat=k):
                left = dict(load)
                for (n, sz, h), d in zip(combo, assign):
                    left[bank] -= sz
                    left[d] += sz
                if any(left[d] > BANK_SIZE for d in set(assign)):
                    continue
                worst = min(BANK_SIZE - left[t] for t in {bank} | set(assign))
                hot_cont = sum(1 for (n, sz, h), d in zip(combo, assign)
                               if h and d in contended)
                freed = sum(sz for _, sz, _ in combo)
                score = (-hot_cont, worst, freed)
                if best is None or score > best[0]:
                    best = (score, [(n, bank, d) for (n, _, _), d in zip(combo, assign)],
                            freed)
        if best:
            break          # fewest moves first; among those, the best score
    return (best[1], best[2]) if best else ([], 0)


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
            print("\n  DOES NOT FIT (first-fit-decreasing is a heuristic; 'plan'"
                  "\n  without --consolidate searches exactly):")
            for n, s in c["unplaced"]:
                print("    %-38s %6d" % (n, s))
    elif free_bank:
        if not moves:
            print("\n  Nothing in %s can move: it holds only pinned or "
                  "unplaceable units." % free_bank)
            return
    else:
        status = c["status"]
        if not moves and status == "ok":
            free = sorted((BANK_SIZE - load[b], b) for b in pool)
            print("\n  Everything fits. No move needed.")
            print("  Slack: %s" % ", ".join("%s %d B" % (b, f) for f, b in
                                            reversed(free)))
            return
        if status == "impossible":
            print("\n  NO PLACEMENT FITS -- proven: no way of putting these units in\n"
                  "  the %d banks keeps every bank under 16 KB. This is a real budget\n"
                  "  wall. Shrink, split, or claim another bank." % len(pool))
            return
        if status == "unknown":
            print("\n  NO PLACEMENT FOUND -- the search spent its %d nodes without\n"
                  "  finding one or proving there is none. That is NOT yet a budget\n"
                  "  wall; raise SEARCH_NODES to settle it." % SEARCH_NODES)
            return
        if status == "ok-unproven":
            print("\n  (this plan fits, but the search ran out of nodes before it\n"
                  "   could rule out every plan with fewer moves)")

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
        # Every bank the plan touches, not just the freed one: the point of the
        # choice is that no destination becomes the next bank to fill.
        print()
        touched = [free_bank] + sorted({d for _, _, d in moves} - {free_bank})
        for b in touched:
            print("  %-14s %6d B free now -> %6d B after%s"
                  % (b, BANK_SIZE - load[b], BANK_SIZE - after[b],
                     "   (freed)" if b == free_bank else "   (receives)"))
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
        die("no placement %s -- nothing to apply. Run 'plan' for the detail."
            % ("fits" if c["status"] == "impossible" else "was found"))
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


def cmd_selftest():
    """The placement search against cases with known answers -- no build
    needed, so CI runs it. A plan is checked the way the linker would judge
    it (every bank under 16 KB, each unit whole, moved from where it was, a
    pinned one never), and small random cases against brute force, which
    knows the true fewest moves and whether any placement exists."""
    import random
    fails = []

    def mk(spec, pinned=(), hot=()):
        us = [Unit(n, [n], sz, b, n in pinned, n in hot) for n, sz, b in spec]
        grown = {u.name: u.size for u in us if not u.pinned}
        return us, grown

    def run(pool, us, grown, contended=()):
        load = {b: sum(u.size for u in us if u.bank == b and not u.pinned)
                for b in pool}
        moves, status = repair(pool, us, load, grown, contended)
        if status.startswith("ok"):
            after, home = dict(load), {u.name: u.bank for u in us}
            pinned = {u.name for u in us if u.pinned}
            names = [n for n, _, _ in moves]
            if len(set(names)) != len(names):
                return moves, status, "a unit moved twice"
            for n, a, b in moves:
                if n in pinned:
                    return moves, status, "moved pinned %s" % n
                if home[n] != a or a == b or b not in pool:
                    return moves, status, "bad move %s %s->%s" % (n, a, b)
                after[a] -= grown[n]
                after[b] += grown[n]
            if any(v > BANK_SIZE for v in after.values()):
                return moves, status, "plan overflows: %s" % after
        return moves, status, None

    def check(label, pool, us, grown, want_status, want_moves=None,
              contended=(), want=None):
        moves, status, err = run(pool, us, grown, contended)
        if err or status != want_status or \
           (want_moves is not None and len(moves) != want_moves) or \
           (want is not None and sorted(moves) != sorted(want)):
            fails.append("%s: got %s %s%s" % (label, status, moves,
                                              " (" + err + ")" if err else ""))

    # issue #17: nothing leaving A fits anywhere as things stand -- B and C
    # must give a unit back. A swap (2 moves) is the answer, not "a wall".
    us, g = mk([("a1", 9000, "A"), ("a2", 8000, "A"), ("b1", 7000, "B"),
                ("b2", 7000, "B"), ("c1", 7000, "C"), ("c2", 7000, "C")])
    check("swap (#17)", ["A", "B", "C"], us, g, "ok", 2)
    # truly impossible: more bytes than banks...
    us, g = mk([("u%d" % i, 9000, "A" if i < 2 else "B") for i in range(4)])
    check("over capacity", ["A", "B"], us, g, "impossible")
    # ...and room in total but no packing: any two of these overflow a bank
    us, g = mk([("x", 10000, "A"), ("y", 10000, "A"), ("z", 10000, "B")])
    check("no packing", ["A", "B"], us, g, "impossible")
    # already fits: nothing moves
    us, g = mk([("a", 9000, "A"), ("b", 9000, "B")])
    check("fits", ["A", "B"], us, g, "ok", 0)
    # a pinned unit (outside the pool) is never a candidate
    us, g = mk([("p", 15000, "X"), ("a1", 9000, "A"), ("a2", 8000, "A"),
                ("b1", 1000, "B")], pinned=("p",))
    check("pinned", ["A", "B"], us, g, "ok", 1, want=[("a2", "A", "B")])
    # a hot unit takes the uncontended bank when both have room
    us, g = mk([("a1", 9000, "A"), ("h", 8000, "A")], hot=("h",))
    check("hot", ["A", "B", "C"], us, g, "ok", 1, contended=("B",),
          want=[("h", "A", "C")])

    # brute force on small random cases: the fewest moves, or "impossible"
    rnd = random.Random(17)
    for case in range(300):
        pool = ["B%d" % i for i in range(rnd.randint(2, 4))]
        spec = [("m%d" % i, rnd.randint(1000, 12000), rnd.choice(pool))
                for i in range(rnd.randint(2, 7))]
        us, g = mk(spec)
        best = None
        for assign in itertools.product(pool, repeat=len(spec)):
            fill = dict.fromkeys(pool, 0)
            for (n, sz, _), b in zip(spec, assign):
                fill[b] += sz
            if all(v <= BANK_SIZE for v in fill.values()):
                k = sum(1 for (_, _, h), b in zip(spec, assign) if b != h)
                best = k if best is None else min(best, k)
        moves, status, err = run(pool, us, g)
        want = "impossible" if best is None else "ok"
        if err or status != want or (best is not None and len(moves) != best):
            fails.append("random #%d %s: got %s %d move(s)%s, brute force %s"
                         % (case, spec, status, len(moves),
                            " (" + err + ")" if err else "", best))

    if fails:
        print("bankpack selftest: %d FAILED" % len(fails))
        for f in fails[:20]:
            print("  " + f)
        sys.exit(1)
    print("bankpack selftest: ok (6 fixed cases, 300 against brute force)")


USAGE = ("usage: bankpack.py [measure|report|plan|apply|compress] [next|zx128]\n"
         "                  plan --consolidate      pack into the fewest banks\n"
         "                  plan --grow mod=BYTES   what if this module grew?\n"
         "                  plan --free BANK_4      empty a named bank\n"
         "                  apply ...               write the plan to banks.json\n"
         "       bankpack.py selftest              check the search (no build)")


def main():
    args = sys.argv[1:]
    cmd = args[0] if args and not args[0].startswith("-") else "report"
    if cmd == "selftest":
        return cmd_selftest()
    if cmd not in ("measure", "report", "plan", "apply", "compress"):
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
            {"measure": cmd_measure, "report": cmd_report,
             "compress": cmd_compress}[cmd](t)


if __name__ == "__main__":
    main()
