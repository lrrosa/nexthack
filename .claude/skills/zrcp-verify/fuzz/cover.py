#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Leonardo Roman da Rosa
"""cover.py <map> <cover files or globs...> - the game's own public functions
that no sample of any fuzz run landed in: what the fuzzing never ran.

The cover files are the runs' log.txt.cover (fuzz.lua's 1 kHz PC sampler).
On the 128K a banked PC carries its bank (BANKM), so it resolves exactly. The
Next's sampler cannot tell its banks apart, so there a banked PC counts for
the function at that offset in EVERY bank: a function listed is still one no
sample can have been in, but a banked one not listed may never have run."""
import bisect
import collections
import glob
import re
import sys

if len(sys.argv) < 3:
    sys.exit('usage: cover.py <nexthack.map|nexthack128.map> <log.txt.cover ...>')
mapf = sys.argv[1]
files = [f for a in sys.argv[2:] for f in (sorted(glob.glob(a)) or [a])]
syms = [m.groups() for m in (re.match(r'^(_\w+)\s+=\s+\$([0-9A-F]+) ; addr, public, , (\w+), (\w+), src/', line)
                              for line in open(mapf)) if m]
# the Next banks its code in PAGE_nn_CODE; its BANK_16..21 are the Layer 2 images
zx128 = not any(sec.startswith('PAGE_') for _, _, _, sec in syms)
funcs = {}
for name, val, mod, sec in syms:
    if not (sec.startswith('code') or sec.startswith('PAGE_') or (zx128 and sec.startswith('BANK_'))):
        continue
    v = int(val, 16)
    b = re.match(r'BANK_(\d+)$', sec)
    if b:                                   # 128K: BANK_n is ORG'd at 0x0nC000
        bank, a = int(b.group(1)), v & 0xFFFF
    elif v >= 0x10000:                      # Next: (page << 16) | address
        bank, a = v >> 16, v & 0xFFFF
    else:
        bank, a = -1, v                     # resident
    funcs.setdefault(bank, []).append((a, name, mod))
for b in funcs:
    funcs[b].sort()


def owner(lst, a):
    i = bisect.bisect_right(lst, (a, '~', '~')) - 1
    return lst[i][1] if i >= 0 else None


hits = collections.Counter()
for f in files:
    for line in open(f):
        k, n = line.split()
        k, n = int(k, 16), int(n)
        a, bank = k & 0xFFFF, k >> 16
        if a < 0xC000:
            banks = [-1]
        elif zx128:
            banks = [bank]
        else:
            banks = [b for b in funcs if b >= 0]
        for b in banks:
            name = owner(funcs.get(b, []), a)
            if name:
                hits[name] += n
allf = sorted({(mod, n) for b in funcs for _, n, mod in funcs[b]})
miss = [(mod, n) for mod, n in allf if hits[n] == 0]
print('%d of %d functions never sampled (%d cover file(s))' % (len(miss), len(allf), len(files)))
by = collections.defaultdict(list)
for mod, n in miss:
    by[mod].append(n)
for mod in sorted(by):
    print('  %-16s %s' % (mod, ' '.join(by[mod])))
