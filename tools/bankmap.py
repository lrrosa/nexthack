#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Leonardo Roman da Rosa
"""bankmap.py - print NextHack's memory budget: the code banks, the resident
half, and the Bank-5 tenant map -- with overlap detection.

Why this exists: Bank 5 (always mapped at 0x4000-0x7FFF on both targets) is
carved up by hard-coded #defines scattered across five source files. When one
tenant grows, the others do NOT move, and nothing in the build catches the
collision. That is exactly how PREV_VIS ended up sitting INSIDE fov_pool for
two releases (0.10.0 grew the pool 4->12 slots; the repaint copy at 0x6C00 then
silently corrupted two parked levels' fog of war).

Every size here is DERIVED from the sources (NTILES, FOV_SLOTS, MAXINV, MAPW/H,
BFSQ_SIZE...), so growing a constant moves the map and the overlap check
follows. Do not hard-code sizes.

Usage:
    python tools/bankmap.py            # both targets, full report
    python tools/bankmap.py --check    # exit 1 on any overlap (CI/build guard)
    python tools/bankmap.py next|zx128 # one target
"""

import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, 'src')

BANK_SIZE = 16384
STACK_FLOOR = 0xBFF0          # REGISTER_SP (zpragma*.inc); resident must end below
STACK_WARN = 0xBDF0           # leave ~512 B of stack headroom


# ---------------------------------------------------------------- source scan

def _read(name):
    with open(os.path.join(SRC, name), encoding='utf-8', errors='replace') as f:
        return f.read()


def const(name, files, default=None):
    """Value of a #define <name> <int> found in the first file that has it."""
    for fn in files:
        try:
            text = _read(fn)
        except OSError:
            continue
        m = re.search(r'^#define\s+%s\s+\(?(\d+)' % re.escape(name),
                      text, re.M)
        if m:
            return int(m.group(1))
    if default is not None:
        return default
    sys.exit('bankmap: constant %s not found in %s' % (name, ', '.join(files)))


def addr(symbol, filename, zx_variant=False):
    """Address from a `#define <symbol> ((type *)0xNNNNu)` in filename.

    item.c defines `inv` twice behind #ifdef __ZXNEXT; zx_variant picks the
    128K one (the first of the pair, inside the #ifndef branch is target
    dependent, so both are collected and chosen by value).
    """
    text = _read(filename)
    hits = re.findall(r'#define\s+%s\s+\(\([^)]*\*\)\s*0x([0-9A-Fa-f]+)u?\)'
                      % re.escape(symbol), text)
    if not hits:
        sys.exit('bankmap: address for %s not found in %s' % (symbol, filename))
    vals = sorted(int(h, 16) for h in hits)
    if len(vals) == 1:
        return vals[0]
    # two variants: the 128K one is the higher address (Bank 5 sits above the
    # ULA screen there), the Next one the lower (it uses the tile-def tail)
    return vals[-1] if zx_variant else vals[0]


def asm_equ(symbol, filename):
    text = _read(filename)
    m = re.search(r'^%s\s+equ\s+0x([0-9A-Fa-f]+)' % re.escape(symbol),
                  text, re.M)
    return int(m.group(1), 16) if m else None


# ------------------------------------------------------------- the tenant map

def bank5_map(target):
    """[(start, size, name, note)] for Bank 5, derived from the sources."""
    ntiles = const('NTILES', ['platform.h'])
    maxinv = const('MAXINV', ['item.c'])
    mapw = const('MAPW', ['level.h'])
    maph = const('MAPH', ['level.h'])
    fov_slots = const('FOV_SLOTS', ['levelfov.c'])
    fov_bytes = (mapw * maph + 7) // 8
    bfsq_size = const('BFSQ_SIZE', ['monster_ai.c'])
    objsz = 5                      # obj_t: otyp, ench, ero, worn, buc

    dist_a = addr('dist', 'monster_ai.c')
    bfsq_a = addr('bfsq', 'monster_ai.c')
    items = []

    if target == 'next':
        tm_w, tm_h = 80, 32
        items += [
            (0x4000, (128 + ntiles) * 32, 'tile definitions',
             '128 font + %d graphic, 32 B each (4bpp)' % ntiles),
            (addr('inv', 'item.c'), maxinv * objsz, 'inv[]',
             'MAXINV %d x %d B' % (maxinv, objsz)),
            (0x5C00, 0, '-- NextZXOS sysvars --', 'tile defs must end below here'),
            (0x6000, tm_w * tm_h * 2, 'tilemap',
             '%dx%d cells x 2 B' % (tm_w, tm_h)),
            (dist_a, mapw * maph, 'dist[] (BFS)', 'MAPW*MAPH'),
            (bfsq_a, bfsq_size * 2, 'bfsq[] (BFS)', 'BFSQ_SIZE %d x 2 B' % bfsq_size),
        ]
    else:
        tm_w = 32
        udg = addr('udg_bitmap', 'platform.h')
        udg_asm = asm_equ('UDG_BITMAP', 'puttile_asm.asm')
        note = 'NTILES %d x 8 B' % ntiles
        if udg_asm is not None and udg_asm != udg:
            note += '  !! puttile_asm.asm disagrees: 0x%04X' % udg_asm
        # the mirrored-UDG annex (T_HERO_R..): ids past the range inv blocks,
        # reached by the blit's own udg_bitmap + (id-128)*8 formula
        mir = re.findall(r'#define\s+T_\w+_R\s+(\d+)', _read('platform.h'))
        items += [
            (0x4000, 6144, 'ULA pixel bitmap', 'SCRN_BASE'),
            (0x5800, 768, 'ULA attributes', 'ATTR_BASE'),
            (addr('VIEW_SHADOW', 'nexthack.c'), maph * tm_w * 2, 'VIEW_SHADOW',
             'MAPH %d x %d cols x 2 B' % (maph, tm_w)),
            (addr('SSHADOW', 'nexthack.c'), 2 * tm_w * 2, 'SSHADOW',
             'status rows 22-23'),
            (udg, ntiles * 8, 'udg_bitmap', note),
            (addr('inv', 'item.c', zx_variant=True), maxinv * objsz, 'inv[]',
             'MAXINV %d x %d B' % (maxinv, objsz)),
        ]
        if mir:
            ids = sorted(int(i) for i in mir)
            items.append((udg + (ids[0] - 128) * 8, len(ids) * 8,
                          'mirrored UDG annex',
                          'ids %d-%d, via udg_bitmap+(id-128)*8'
                          % (ids[0], ids[-1])))
        items += [
            (addr('fov_pool', 'levelfov.c'), fov_slots * fov_bytes, 'fov_pool',
             'FOV_SLOTS %d x %d B' % (fov_slots, fov_bytes)),
            (addr('PREV_VIS', 'nexthack.c'), fov_bytes, 'PREV_VIS',
             'one vis bitmap'),
            (dist_a, mapw * maph, 'dist[] (BFS)', 'MAPW*MAPH'),
            (bfsq_a, bfsq_size * 2, 'bfsq[] (BFS)', 'BFSQ_SIZE %d x 2 B' % bfsq_size),
        ]

    return sorted(items, key=lambda t: (t[0], t[1]))


def report_bank5(target):
    items = bank5_map(target)
    print('  Bank 5 (0x4000-0x8000, always mapped) -- %s' % target)
    overlaps = []
    prev_end = 0x4000
    prev_name = 'start of bank'
    for start, size, name, note in items:
        end = start + size
        if size and start < prev_end:
            overlaps.append((name, prev_name, start, prev_end))
            flag = '  <== OVERLAPS %s' % prev_name
        elif size and start > prev_end:
            flag = '   (%d B free above %s)' % (start - prev_end, prev_name)
        else:
            flag = ''
        if size:
            print('    0x%04X-0x%04X  %-22s %5d B  %s%s'
                  % (start, end, name, size, note, flag))
            prev_end, prev_name = end, name
        else:
            print('    0x%04X         %-22s        %s' % (start, name, note))
    tail = 0x8000 - prev_end
    if tail > 0:
        print('    0x%04X-0x8000  %-22s %5d B  free tail'
              % (prev_end, '(free)', tail))
    return overlaps


# ------------------------------------------------------------- the code banks

def report_code_banks(prefix, label):
    """Sizes of the emitted per-bank binaries (the linker overflows silently)."""
    print('  Code banks -- %s' % label)
    found = False
    over = []
    for fn in sorted(os.listdir(ROOT)):
        m = re.match(r'^%s_((?:PAGE|BANK)_\w+)\.bin$' % re.escape(prefix), fn)
        if not m:
            continue
        name = m.group(1)
        # the Next's BANK_16..21 are the Layer 2 image thirds, not code
        if prefix == 'nexthack' and re.match(r'^BANK_(1[6-9]|2[01])$', name):
            continue
        size = os.path.getsize(os.path.join(ROOT, fn))
        free = BANK_SIZE - size
        found = True
        if free < 0:
            over.append((name, -free))
            note = '  <== OVERFLOWS by %d B' % -free
        elif free < 256:
            note = '  <== effectively FULL'
        elif free < 1024:
            note = '  (tight)'
        else:
            note = ''
        print('    %-14s %6d / %d B   %6d B free%s'
              % (name, size, BANK_SIZE, free, note))
    if not found:
        print('    (no .bin files -- build the target first)')
    return over


def report_resident(mapfile, label):
    path = os.path.join(ROOT, mapfile)
    if not os.path.exists(path):
        print('  Resident half -- %s: no %s (build first)' % (label, mapfile))
        return []
    with open(path, encoding='utf-8', errors='replace') as f:
        text = f.read()
    out, warn = {}, []
    for sym in ('__CODE_END_tail', '__BSS_END_tail'):
        m = re.search(r'^%s\s+=\s+\$([0-9A-Fa-f]+)' % re.escape(sym), text, re.M)
        if m:
            out[sym] = int(m.group(1), 16)
    bss = out.get('__BSS_END_tail')
    print('  Resident half -- %s' % label)
    if bss is None:
        print('    (symbols not found)')
        return warn
    free = STACK_FLOOR - bss
    note = ''
    if bss >= STACK_FLOOR:
        note = '  <== PAST THE STACK FLOOR -- will corrupt the stack!'
        warn.append(('resident %s past the stack floor' % label, bss))
    elif bss >= STACK_WARN:
        # advice, not a defect: the project has shipped this tight before
        note = '  (tight: under the %d B stack reserve)' % (STACK_FLOOR - STACK_WARN)
    print('    __CODE_END=$%04X  __BSS_END=$%04X   %d B to the stack floor '
          '($%04X)%s' % (out.get('__CODE_END_tail', 0), bss, free,
                         STACK_FLOOR, note))

    # The Next streams the Layer 2 palettes with bank 11 mapped: if PAGE_22
    # grows past 16 KB they spill into bank 12 and the title colours scramble.
    bad = []
    for sym in ('_title_pal', '_victory_pal'):
        m = re.search(r'^%s\s+=\s+\$([0-9A-Fa-f]+)' % sym, text, re.M)
        if m:
            v = int(m.group(1), 16)
            state = 'ok' if v < 0x170000 else '<== SPILLED past bank 11!'
            if v >= 0x170000:
                bad.append((sym, v))
            print('    %-14s $%06X  (must stay < $170000)  %s'
                  % (sym, v, state))
    warn.extend(bad)
    return warn


# ---------------------------------------------------------------------- main

def run(target):
    print()
    if target == 'next':
        print('== ZX Spectrum Next (nexthack.nex) ' + '=' * 34)
        problems = report_resident('nexthack.map', 'Next')
        print()
        problems += report_code_banks('nexthack', 'Next (PAGE_20=b10, 22=b11, '
                                                 '26=b13, 28=b14)')
    else:
        print('== ZX Spectrum 128K (nexthack128.tap) ' + '=' * 31)
        problems = report_resident('nexthack128.map', '128K')
        print()
        problems += report_code_banks('nexthack128', '128K (paged via 0x7FFD)')
    print()
    problems += report_bank5(target)
    return problems


def main(argv):
    check = '--check' in argv
    args = [a for a in argv if not a.startswith('-')]
    targets = args if args else ['next', 'zx128']
    problems = []
    for t in targets:
        if t not in ('next', 'zx128'):
            sys.exit('bankmap: unknown target %r (use next or zx128)' % t)
        problems += run(t)
    print()
    if problems:
        print('!! %d problem(s) above: %s'
              % (len(problems), ', '.join(str(p[0]) for p in problems)))
        print('   see .claude/skills/bank-budget for the relocation procedure')
        return 1 if check else 0
    print('No overlaps or overflows.')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
