#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Leonardo Roman da Rosa
#
# mktap128.py - assemble a 128K-aware .tap for the +zx build.
#
# z88dk's newlib +zx tape loader is the plain 48K one: it loads only the
# resident CODE, not the extra 16K banks, so the banked half of the game is
# absent and the first __banked call crashes. This replaces that .tap with one
# whose BASIC loader pages each bank via port 0x7FFD and LOADs its block (each
# given a real header so the ROM `LOAD ""CODE` finds it), then hands off through
# a tiny boot stub that sets the interrupt mode the game expects and JPs in.
# Standard ROM blocks -> fast-loads on any 128K emulator and real hardware.
#
# Tape layout:
#   Program "nexthack" - the BASIC loader (autostart line 10)
#   Bytes   "nexthack" - resident CODE -> 0x8000
#   Bytes   "bank1/3/4"- the three banked code/data banks -> 0xC000
#   Bytes   "boot"     - the boot stub -> 0x5B00 (run last, JPs to the game)
#
#   python tools/mktap128.py        (run from the repo root, after the link)

import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CODE = 0x8000          # resident load address
BANKW = 0xC000         # bank window
BANKM = 23388          # 0x5B5C: the 0x7FFD shadow system variable

# Boot stub: the 128K editor runs in IM2; z88dk's +zx crt0 does NOT set the
# interrupt mode, so handing off straight from BASIC inherits IM2 and the first
# maskable interrupt vectors into garbage. This forces the .sna's clean state
# (DI : IM 1 : LD A,0x3F : LD I,A : JP 0x8000) before entering the game.
STUB_ADDR = 0x5B00     # 23296: printer buffer -- free, survives the loads
STUB = bytes([0xF3, 0xED, 0x56, 0x3E, 0x3F, 0xED, 0x47, 0xC3, 0x00, 0x80])

CODE_BIN = "nexthack128_CODE.bin"


def banks_from_manifest():
    """Which banks the tape must carry -- derived from banks.json, not listed.

    The two failure modes are opposite and both silent-ish: a bank that holds
    code but is missing from the tape is simply ABSENT at runtime, and the first
    __banked call into it crashes; a bank listed here that no module was assigned
    to produces no .bin at all, and the loader stops on a block that does not
    exist. Reading the same manifest the compiler was driven from keeps the tape
    and the assignment from drifting apart -- which matters now that a repack
    (tools/bankpack.py) can empty a bank.
    """
    with open(os.path.join(ROOT, "banks.json")) as f:
        zx = json.load(f)["zx128"]
    used = set()
    for mod, e in zx.items():
        if not mod.startswith("_"):
            used.update(b for b in (e.get("code"), e.get("const")) if b)
    out = []
    for name in used:
        m = re.match(r"^BANK_(\d+)$", name)
        if not m:
            sys.exit("mktap128: %r is not a 128K bank name (BANK_n)." % name)
        out.append(("nexthack128_%s.bin" % name, "bank%s" % m.group(1),
                    int(m.group(1))))
    return sorted(out, key=lambda b: b[2])


BANKS = banks_from_manifest()

# ---- ZX BASIC tokens ----
CLEAR, VAL, LOAD, CODE_T, OUT, POKE, PEEK, AND, LET, RAND, USR = \
    0xFD, 0xB0, 0xEF, 0xAF, 0xDF, 0xF4, 0xBE, 0xC6, 0xF1, 0xF9, 0xC0


def q(t):                      # a "string" literal
    return bytes([0x22]) + str(t).encode("ascii") + bytes([0x22])


def basic_line(num, body):     # num big-endian, length little-endian, end 0x0D
    inner = bytes(body) + bytes([0x0D])
    return bytes([num >> 8, num & 0xFF, len(inner) & 0xFF, len(inner) >> 8]) + inner


def loader_program():
    """10 CLEAR VAL"32767": LET b=PEEK VAL"23388" AND VAL"16"
       20 LOAD ""CODE                                    (resident -> 0x8000)
       30 OUT VAL"32765",b+VAL"1": POKE VAL"23388",b+VAL"1": LOAD ""CODE  (bank 1)
       40/50/... one line per bank    then LOAD ""CODE (boot stub)
       ... OUT VAL"32765",VAL"16": POKE VAL"23388",VAL"16": RANDOMIZE USR VAL"23296"
    The final line pages bank 0 + the 48K ROM and runs the stub.

    Why `b` exists, and why it masks to bit 4 ONLY (see the RAM-interface note
    in CLAUDE.md). Bit 4 (ROM select) genuinely must be preserved: boot a real
    128K into "48K BASIC" and it is already 1, so forcing it to 0 mid-load
    would swap the ROM out from under the running loader. Every OTHER bit must
    be forced to 0, because we read them out of BANKM -- a 128K-ROM system
    variable that does NOT exist on a 48K machine fitted with a RAM-expansion
    interface (tkmem128-ii and friends). There, 23388 is the printer buffer and
    holds garbage, and the old `AND 248` let that garbage reach:
      bit 3  shadow screen  -- we never want it (and such interfaces can't do it)
      bit 5  PAGING LOCK    -- one stray 1 and the very first OUT freezes the
                               latch forever: the remaining banks never load
      bits 6-7 512K extension on those interfaces -- would page the wrong 16K
    On a healthy real 128K the two masks behave identically (bits 3/5/6/7 are
    already 0 at this point), so this is pure robustness."""
    p = basic_line(10, bytes([CLEAR, VAL]) + q("32767") + b":" +
                       bytes([LET]) + b"b=" + bytes([PEEK, VAL]) + q("23388") +
                       bytes([AND, VAL]) + q("16"))
    p += basic_line(20, bytes([LOAD]) + q("") + bytes([CODE_T]))
    ln = 30
    for _, _, bank in BANKS:
        d = str(bank)
        p += basic_line(ln,
            bytes([OUT, VAL]) + q("32765") + b",b+" + bytes([VAL]) + q(d) + b":" +
            bytes([POKE, VAL]) + q("23388") + b",b+" + bytes([VAL]) + q(d) + b":" +
            bytes([LOAD]) + q("") + bytes([CODE_T]))
        ln += 10
    p += basic_line(ln, bytes([LOAD]) + q("") + bytes([CODE_T]))   # boot stub
    ln += 10
    p += basic_line(ln,
        bytes([OUT, VAL]) + q("32765") + b"," + bytes([VAL]) + q("16") + b":" +
        bytes([POKE, VAL]) + q("23388") + b"," + bytes([VAL]) + q("16") + b":" +
        bytes([RAND, USR, VAL]) + q(str(STUB_ADDR)))
    return p


def block(flag, data):         # a .tap block: len, flag, data, xor-checksum
    body = bytes([flag]) + data
    chk = 0
    for x in body:
        chk ^= x
    return bytes([(len(body) + 1) & 0xFF, (len(body) + 1) >> 8]) + body + bytes([chk])


def header(htype, name, length, p1, p2=0x8000):
    nm = (name + " " * 10)[:10].encode("ascii")
    return block(0x00, bytes([htype]) + nm +
                 bytes([length & 0xFF, length >> 8, p1 & 0xFF, p1 >> 8,
                        p2 & 0xFF, p2 >> 8]))


def main():
    prog = loader_program()
    tap = header(0, "nexthack", len(prog), 10, len(prog)) + block(0xFF, prog)

    code = open(os.path.join(ROOT, CODE_BIN), "rb").read()
    tap += header(3, "nexthack", len(code), CODE) + block(0xFF, code)
    for fname, name, _ in BANKS:
        path = os.path.join(ROOT, fname)
        if not os.path.exists(path):
            sys.exit("mktap128: %s is missing. banks.json assigns a module to that "
                     "bank but the linker emitted nothing for it -- link first, or "
                     "fix the assignment." % fname)
        data = open(path, "rb").read()
        if len(data) > 16384:
            # The linker happily emits an ORG'd bank section past its 16 KB
            # window and LOAD "" CODE at 0xC000 truncates it at 0xFFFF -- the
            # clipped tail (v0.5.0: template data) crashes at runtime. Refuse.
            sys.exit("mktap128: %s is %d bytes -- overflows its 16 KB bank by %d! "
                     "Move a module to a roomier BANK_n." %
                     (fname, len(data), len(data) - 16384))
        tap += header(3, name, len(data), BANKW) + block(0xFF, data)
    tap += header(3, "boot", len(STUB), STUB_ADDR) + block(0xFF, STUB)

    out = os.path.join(ROOT, "nexthack128.tap")
    open(out, "wb").write(tap)
    print("mktap128: wrote %s (%d bytes, %d-bank BASIC loader + boot stub)"
          % (os.path.basename(out), len(tap), len(BANKS)))


if __name__ == "__main__":
    main()
