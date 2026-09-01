#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Leonardo Roman da Rosa
#
# lztmpl.py - the tiny LZSS the game decompresses the level templates with.
#
# Why not ZX0, which packs better: the decompressor has to be written and proved
# here too, and this format is small enough to verify exhaustively -- compress()
# and expand() below round-trip every template, and expand() is a line-for-line
# model of lz_expand() in src/leveltmpl.c. A format we can prove beats a format
# that packs 20% better and might be subtly wrong on one template.
#
# Why not RLE, which is simpler still: the maze template is 76% of its size under
# RLE and 15% under LZ. RLE collapses exactly on the densest map, which is the
# one that most needs the room.
#
# Stream layout, byte oriented because the Z80 is:
#   control byte, then 8 items, LSB of the control byte first
#     bit 1 -> one literal byte
#     bit 0 -> two bytes: a back-reference into the OUTPUT
#   match: b0 = (offset-1) low 8, b1 = (offset-1) high 3 << 5 | (len - 3)
#     offset 1..2048 -- biased by one so the full 11 bits are usable; the
#     unbiased form silently overflowed b1 at exactly 2048
#     len    3..34   (long runs cost few tokens: 80 spaces = 3 matches)
#
# A match may overlap the write head (offset 1 = "repeat the last byte"), which
# is what makes runs cheap; expand() copies one byte at a time, never memmove.

import sys

OFF_BITS, LEN_BITS = 11, 5
MAX_OFF = 1 << OFF_BITS
MIN_LEN = 3
MAX_LEN = MIN_LEN + (1 << LEN_BITS) - 1


def _index(data):
    """positions by 3-byte prefix, most recent first.

    Exact, not a heuristic: MIN_LEN is 3, so every match we can encode starts
    with the same three bytes. Scanning only that bucket gives the identical
    answer brute force would, which matters -- bankpack compresses whole 15 KB
    sections with this, and O(n^2) over the window was minutes of Python.
    """
    idx = {}
    for i in range(len(data) - 2):
        idx.setdefault(data[i:i + 3], []).append(i)
    for v in idx.values():
        v.reverse()
    return idx


def _longest(data, pos, idx):
    """(len, offset) of the longest back-reference at pos, or (0, 0)."""
    best_len, best_off = 0, 0
    lo = pos - MAX_OFF
    for start in idx.get(data[pos:pos + 3], ()):
        if start >= pos:
            continue
        if start < lo:
            break                      # bucket is newest-first: rest is out of range
        n = 0
        while (pos + n < len(data) and n < MAX_LEN
               and data[start + n] == data[pos + n]):
            n += 1
        if n > best_len:
            best_len, best_off = n, pos - start
            if n == MAX_LEN:
                break
    return (best_len, best_off) if best_len >= MIN_LEN else (0, 0)


def compress(data):
    idx = _index(data)
    items, pos = [], 0
    while pos < len(data):
        n, off = _longest(data, pos, idx)
        # lazy: a strictly longer match one byte on is worth a literal here
        if n:
            n2, _ = _longest(data, pos + 1, idx)
            if n2 > n:
                n = 0
        if n:
            items.append((off, n))
            pos += n
        else:
            items.append(data[pos])
            pos += 1

    out = bytearray()
    for i in range(0, len(items), 8):
        group = items[i:i + 8]
        ctrl = 0
        for b, it in enumerate(group):
            if isinstance(it, int):
                ctrl |= 1 << b
        out.append(ctrl)
        for it in group:
            if isinstance(it, int):
                out.append(it)
            else:
                off, n = it
                o = off - 1
                out.append(o & 0xFF)
                out.append(((o >> 8) << 5) | (n - MIN_LEN))
    return bytes(out)


def expand(blob, size):
    """Reference decoder -- src/leveltmpl.c's lz_expand() must match it."""
    out = bytearray()
    p = 0
    while len(out) < size:
        ctrl = blob[p]; p += 1
        for b in range(8):
            if len(out) >= size:
                break
            if ctrl & (1 << b):
                out.append(blob[p]); p += 1
            else:
                b0, b1 = blob[p], blob[p + 1]; p += 2
                off = (b0 | ((b1 >> 5) << 8)) + 1
                n = (b1 & 0x1F) + MIN_LEN
                for _ in range(n):
                    out.append(out[len(out) - off])
    return bytes(out)


if __name__ == "__main__":
    # self-test on whatever files are named, else a quick sanity check
    for path in sys.argv[1:]:
        d = open(path, "rb").read()
        c = compress(d)
        assert expand(c, len(d)) == d, path
        print("%-30s %6d -> %6d (%.1f%%)" % (path, len(d), len(c),
                                             100.0 * len(c) / len(d)))
