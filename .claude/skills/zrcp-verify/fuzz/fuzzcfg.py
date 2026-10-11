#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Leonardo Roman da Rosa
"""fuzzcfg.py <zx128|next> <portdir> <out.lua> - the fuzzer's config as a Lua
table, all of it read from the tree the binary was built from (<portdir>):

  * symbol addresses from the target's .map, plus the size of the persistence
    arrays a scan trip clears (the distance to the next symbol);
  * the T_* tile numbers (platform.h), the monster char -> tile table
    (montypes[] in monster.c) and the object catalogue's names (item.c);
  * the Bank-5 tenants the checks read (inv[], dug_pool, the 128K's fov_pool,
    VIEW_SHADOW and udg_bitmap) from tools/bankmap.py, never typed in here --
    inv[] moved in 1.6, and an old address now reads the UDG bitmap;
  * the memory the game must never write: resident code, the code banks,
    Bank 5's gaps between tenants, the free space under the stack's reserve.

fuzz.ps1 runs it before every run, so a rebuild never leaves it stale."""
import os
import re
import sys

if len(sys.argv) != 4 or sys.argv[1] not in ('zx128', 'next'):
    sys.exit('usage: fuzzcfg.py <zx128|next> <portdir> <out.lua>')
target, port, out = sys.argv[1], os.path.abspath(sys.argv[2]), sys.argv[3]
src = os.path.join(port, 'src')
sys.path.insert(0, os.path.join(port, 'tools'))
import bankmap                                    # noqa: E402 -- the Bank-5 tenant map

mapf = os.path.join(port, 'nexthack128.map' if target == 'zx128' else 'nexthack.map')
if not os.path.exists(mapf):
    sys.exit('fuzzcfg: %s missing (build first)' % mapf)

syms, order, allsyms, consts = {}, [], [], {}
for line in open(mapf):
    m = re.match(r'^(\S+)\s+=\s+\$([0-9A-F]+) ; addr, (\w+), ', line)
    if m:
        v = int(m.group(2), 16)
        syms.setdefault(m.group(1), v)
        allsyms.append((v, m.group(1)))
        if m.group(3) == 'public':
            order.append((v, m.group(1)))   # function ends at the next PUBLIC symbol
        continue
    m = re.match(r'^(\S+)\s+=\s+\$([0-9A-F]+) ; const', line)
    if m:
        consts.setdefault(m.group(1), int(m.group(2), 16))
order = sorted(set(order))
allsyms = sorted(set(allsyms))


def need_sym(name):
    if name not in syms:
        sys.exit('fuzzcfg: %s not in %s (renamed? update fuzzcfg.py)' % (name, mapf))
    return syms[name]


def frange(name):
    """[start, next public symbol) of a resident function"""
    a = need_sym(name)
    return a, min(v for v, n in order if a < v < 0x10000)


def func_end(name):
    """the next C symbol in the same bank: the end of a (banked or static) function"""
    a = need_sym(name)
    return min(v for v, n in allsyms if n.startswith('_') and v > a and v >> 16 == a >> 16)


def size_of(name):
    """an array's size: the distance to the next symbol (they are laid out back to back)"""
    a = need_sym(name)
    return min(v for v, n in allsyms if v > a) - a


need = ['_hero_x', '_hero_y', '_hero_face', '_dlvl', '_php', '_pmaxhp', '_nutrition',
        '_mcount', '_m_x', '_m_y', '_m_alive', '_m_type', '_m_face', '_inv_count',
        '_pet_idx', '_have_pet', '_lvl', '_dn_x', '_dn_y', '_mn_x', '_mn_y', '_up_x', '_up_y',
        '_turns', '_world_seed', '_rng', '_gold', '_resting', '_dead', '_won', '_st_blind',
        '_st_sleep', '_intrinsics', '_amu_esp', '_shop_room', '_r_x', '_r_y', '_r_w', '_r_h',
        '_rcount', '_vis_now', '_cur_slot', '_has_amulet', '_floor_n', '_stash', '_gold_taken',
        '_item_taken', '_mon_dead', '_door_open', '_el_life', 'tempsp']
if target == 'zx128':
    need += ['_vx_origin', '_udg_ink']
else:
    need += ['_fov_pool_buf']
S = {n.lstrip('_'): need_sym(n) for n in need}
# what a scan trip forgets when it switches worlds (fuzz.lua arm_trip)
sizes = {n.lstrip('_'): size_of(n) for n in
         ('_stash', '_gold_taken', '_item_taken', '_mon_dead', '_door_open')}

tiles = {}
for line in open(os.path.join(src, 'platform.h'), encoding='utf-8'):
    m = re.match(r'#define\s+(T_\w+)\s+(\d+)', line)
    if m:
        tiles[m.group(1)] = int(m.group(2))
mon = {}
for m in re.finditer(r"\{\s*('(.)'|MON_KEEPER),\s*\d+,\s*\d+,\s*\d+,\s*\d+,\s*(T_\w+)",
                     open(os.path.join(src, 'monster.c'), encoding='utf-8').read()):
    mon[m.group(2) or '@'] = tiles[m.group(3)]      # MON_KEEPER is '@' (monster.h)
if not mon:
    sys.exit('fuzzcfg: no montypes[] rows parsed out of monster.c')

code_end = syms.get('__CODE_END_head') or consts.get('__CODE_END_head')
bss_end = syms.get('__BSS_END_head') or consts.get('__BSS_END_head')
if code_end is None or bss_end is None:
    for line in open(mapf):
        m = re.match(r'^(__CODE_END_head|__BSS_END_head)\s+=\s+\$([0-9A-F]+)', line)
        if m:
            if m.group(1) == '__CODE_END_head':
                code_end = int(m.group(2), 16)
            else:
                bss_end = int(m.group(2), 16)
if code_end is None or bss_end is None:
    sys.exit('fuzzcfg: __CODE_END_head / __BSS_END_head not in the map')

guard = []                                         # (lo, hi, label)
guard.append((0x8000, code_end, 'resident-code'))
guard.append((0xC000, 0x10000, 'code-bank'))
guard.append((bss_end, bankmap.STACK_FLOOR, 'under-stack-reserve'))
prev = 0x4000
for start, size, name, note in bankmap.bank5_map(target):
    if not size:
        continue
    if start > prev:
        guard.append((prev, start, 'bank5-gap-before-' + re.sub(r'\W+', '_', name)))
    prev = max(prev, start + size)
if prev < 0x8000:
    guard.append((prev, 0x8000, 'bank5-tail'))

idle = [frange(n) for n in ('_getkey_rpt', '_getkey', 'asm_in_inkey', 'asm_in_wait_nokey')]
main_idle = frange('_getkey_rpt')

cat = open(os.path.join(src, 'item.c'), encoding='utf-8').read()
body = cat[cat.index('objtypes[NUMOBJ] = {'):]
body = body[:body.index('\n};')]
objs = {nm: i for i, nm in enumerate(re.findall(r'"([^"]*)"\s*\}', body))}

mapw = bankmap.const('MAPW', ['level.h'])
maph = bankmap.const('MAPH', ['level.h'])
b5 = {
    'inv': bankmap.addr('inv', 'item_int.h', zx_variant=(target == 'zx128')),
    'dug_pool': bankmap.addr('dug_pool', 'levelfov.c'),
    'dug_max': bankmap.const('DUG_MAX', ['levelfov.c']),
    'fov_bytes': (mapw * maph + 7) // 8,
}
if target == 'zx128':
    b5['fov_pool'] = bankmap.addr('fov_pool', 'levelfov.c')
    b5['view_shadow'] = bankmap.addr('VIEW_SHADOW', 'nexthack.c')
    b5['udg'] = bankmap.addr('udg_bitmap', 'platform.h')
else:
    b5['fov_pool'] = S['fov_pool_buf']


def lua(v):
    if isinstance(v, dict):
        return '{' + ', '.join('[%s]=%s' % (lua(k), lua(x)) for k, x in v.items()) + '}'
    if isinstance(v, (list, tuple)):
        return '{' + ', '.join(lua(x) for x in v) + '}'
    if isinstance(v, str):
        return '"' + v.replace('\\', '\\\\').replace('"', '\\"') + '"'
    return str(v)


cfg = {
    'target': target, 'sym': S, 'size': sizes, 'tile': tiles, 'mon': mon, 'guard': guard,
    'idle': idle, 'main_idle': main_idle, 'code_end': code_end, 'bss_end': bss_end,
    'stack_floor': bankmap.STACK_FLOOR, 'sp_init': bankmap.SP_INIT,
    'banking_stack': consts.get('CLIB_BANKING_STACK_SIZE', 100),
    'objs': objs, 'numobj': len(objs),
    'maxmon': bankmap.const('MAXMON', ['monster.h']),
    'maxinv': bankmap.const('MAXINV', ['item_int.h', 'item.c']),
    # getkey_rpt also waits under cursor_pick (farlook, teleport control):
    # a return address in here on the stack tells the two apart
    'cursor': [need_sym('_cursor_pick') & 0xFFFF, func_end('_cursor_pick') & 0xFFFF],
}
cfg.update(b5)
with open(out, 'w') as f:
    f.write('return ' + lua(cfg) + '\n')
print('guard ranges:')
for lo, hi, lb in guard:
    print('  %04X-%04X %5d B  %s' % (lo, hi, hi - lo, lb))
print('idle', ['%04X-%04X' % r for r in idle], 'main', '%04X-%04X' % main_idle)
print('inv %04X dug_pool %04X fov_pool %04X  sizes %s' % (b5['inv'], b5['dug_pool'], b5['fov_pool'], sizes))
print('mon tiles', len(mon), 'syms', len(S), 'objs', len(objs))
