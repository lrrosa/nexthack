#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Leonardo Roman da Rosa
"""mameprof.py - read a mame.lua timing log: where the CPU went, and how fast
the hero stepped.

    python mameprof.py <log.txt> <zx128|next> [--map FILE] [--top N]

Profile: each `sampleoff <label>` block is a histogram of sampled PCs. Banked
PCs (>= 0xC000) carry the 128K's paged bank, so they resolve against the right
BANK_n symbols (the Next's banks are not tracked: read its banked rows as
"some bank"). Time spent waiting for a key -- getkey_rpt and in_inkey's scan --
and in the ROM's IM1 handler is idle and left out; the rest is the turn's work,
in % and in ms of emulated time.

Steps: the step timing expects the watch list to begin `watch @hero_x 2`,
`watch @hero_y 2`, `watch @turns 2`, and on the 128K optionally
`watch @prev_hx 1` (draw_map's last drawn hero column): then every `hold`
reports its step periods, and every move how long it took from the hero's move
to the frame that drew it (a horizontal move only -- prev_hx is a column).
"""
import argparse
import bisect
import collections
import os
import re

ROOT = os.path.normpath(os.path.join(os.path.dirname(__file__), '..', '..', '..'))
IDLE = {'_getkey_rpt', 'asm_in_inkey', 'row_loop', 'keyhit_0', 'keyhit_1',
        'check_caps', 'check_sym', 'ascii', 'error_znc', 'ROM'}
LOCAL = re.compile(r'^(l_\w+_\d{5}|i_\d+|_{2,3}str_\d+)$')


def load_syms(mapfile):
    """{bank or -1: sorted [(addr, name)]}, functions only (no l_* locals)."""
    syms = {}
    for line in open(mapfile):
        m = re.match(r'^(\S+)\s+=\s+\$([0-9A-F]+) ; addr, \w+, , \S*, \S*,', line)
        if not m or LOCAL.match(m.group(1)):
            continue
        v = int(m.group(2), 16)
        bank, a = (v >> 16, v & 0xFFFF) if v >= 0x10000 else (-1, v)
        syms.setdefault(bank, []).append((a, m.group(1)))
    for lst in syms.values():
        lst.sort()
    return syms


def name_of(syms, key):
    a, bank = key & 0xFFFF, key >> 16
    if a < 0x4000:
        return 'ROM'
    if a < 0x8000:
        return 'bank5:%04X' % a
    lst = syms.get(bank if a >= 0xC000 else -1, [])
    i = bisect.bisect_right(lst, (a, '\x7f')) - 1
    return lst[i][1] if i >= 0 else '?'


def num(s):
    return float(s.replace(',', '.'))      # MAME logs in the host's locale


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('log')
    ap.add_argument('target', choices=['zx128', 'next'])
    ap.add_argument('--map')
    ap.add_argument('--top', type=int, default=15)
    ap.add_argument('--hz', type=int, default=1000, help='if the log does not say')
    a = ap.parse_args()
    mapfile = a.map or os.path.join(ROOT, 'nexthack128.map' if a.target == 'zx128'
                                    else 'nexthack.map')
    syms = load_syms(mapfile)
    lines = open(a.log).read().splitlines()

    hz = float(a.hz)
    i = 0
    while i < len(lines):
        l = lines[i]
        if l.startswith('SON ') and len(l.split()) == 3:
            hz = float(l.split()[1])         # SON <hz> <time>
        if l.startswith('HIST '):
            _, label, n = l.split()
            fn = collections.Counter()
            i += 1
            while not lines[i].startswith('HEND'):
                _, k, c = lines[i].split()
                fn[name_of(syms, int(k, 16))] += int(c)
                i += 1
            busy = sum(c for nm, c in fn.items() if nm not in IDLE)
            print('== %s: %d samples at %d Hz, busy %.0f ms' % (label, int(n), hz,
                                                               1000 * busy / hz))
            for nm, c in [x for x in fn.most_common() if x[0] not in IDLE][:a.top]:
                print('  %5.1f%%  %7.1f ms  %s' % (100.0 * c / max(busy, 1),
                                                   1000 * c / hz, nm))
        i += 1

    evs, seg = [], 0          # (time, x, y, prev_hx, sampling segment)
    for l in lines:
        if l.startswith('SON '):
            seg += 1              # a new segment: its first W is a baseline, not a move
        elif l.startswith('W '):
            p = l.split()
            b = [int(x, 16) for x in p[2:]]
            if len(b) >= 6:
                evs.append((num(p[1]), b[0] | b[1] << 8, b[2] | b[3] << 8,
                            b[6] if len(b) > 6 else None, seg))

    def moved(e0, e1):
        return e0[4] == e1[4] and e0[1:3] != e1[1:3]

    drawn = {}
    for j, (e0, e1) in enumerate(zip(evs, evs[1:])):
        if moved(e0, e1) and e1[3] is not None:
            for e in evs[j + 1:]:
                if e[4] != e1[4]:
                    break
                if e[3] == (e1[1] & 0xFF):
                    drawn[e1[0]] = 1000 * (e[0] - e1[0])
                    break
    holds = [(num(l.split()[2]), l.split()[1]) for l in lines if l.startswith('HOLD ')]
    rels = [num(l.split()[1]) for l in lines if l.startswith('REL ')]
    for (h0, key), r in zip(holds, rels):
        moves = [e1[0] for e0, e1 in zip(evs, evs[1:])
                 if moved(e0, e1) and h0 <= e1[0] <= r + 0.02]
        if len(moves) < 2:
            print('hold %s: %d moves' % (key, len(moves)))
            continue
        d = [1000 * (y - x) for x, y in zip(moves, moves[1:])]
        print('hold %s %.2f s: %d moves, first after %.0f ms; then every %s ms'
              % (key, r - h0, len(moves), 1000 * (moves[0] - h0),
                 ' '.join('%.0f' % x for x in d)))
        dm = [drawn[t] for t in moves if t in drawn]
        if dm:
            print('   move -> drawn ms: ' + ' '.join('%.0f' % x for x in dm))
    if not holds and drawn:
        print('move -> drawn ms: ' + ' '.join('%.0f' % x for x in drawn.values()))


if __name__ == '__main__':
    main()
