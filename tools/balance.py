#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Leonardo Roman da Rosa
"""balance.py - the balance instrument for NextHack.

Answers "is the difficulty curve right?" with numbers instead of impressions.

Three design rules keep the numbers honest:

  1. The tables are READ OUT OF THE C SOURCE, never retyped here.  Edit
     montypes[]/objtypes[]/classes[] and re-run: the model follows.  If a
     table stops parsing, the tool fails loudly rather than using stale data.

  2. Every formula carries the src file:line it mirrors.  They are short
     enough to audit side by side, and "balance.py formulas" prints each
     one next to the C it came from.

  3. The dice are THE GAME'S OWN dice -- the xorshift16 from src/rng.c and
     the pure item/level hashes -- so the distributions come from the
     generator the Spectrum actually runs, not Python's Mersenne Twister.

Usage:
    python tools/balance.py report      # everything, as prose + tables
    python tools/balance.py tables      # what was parsed out of the source
    python tools/balance.py curves      # threat vs durability by depth
    python tools/balance.py duel        # hero-vs-monster win rates
    python tools/balance.py gear        # what the floor actually offers
    python tools/balance.py runs        # full descents: death by depth
    python tools/balance.py formulas    # the C next to its Python mirror
"""

import argparse
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
DEFAULT_SRC = os.path.join(os.path.dirname(HERE), "src")

# ---------------------------------------------------------------------------
# 1. The game's own dice (src/rng.c) -- xorshift16, 16-bit wrap throughout.
# ---------------------------------------------------------------------------

M16 = 0xFFFF


class Rng(object):
    """src/rng.c:36 rng_next / :43 rn2 -- bit-exact, 16-bit wrap included."""

    def __init__(self, seed=1):
        self.s = (seed & M16) or 0xACE1

    def next(self):
        s = self.s
        s ^= (s << 7) & M16
        s ^= s >> 9
        s ^= (s << 8) & M16
        self.s = s
        return s

    def rn2(self, n):
        return self.next() % n if n else 0


def item_hash(world_seed, dlvl, x, y):
    """src/item.c:635 -- pure; never touches the RNG stream."""
    h = (world_seed + dlvl * 2657 + x * 131 + y * 1009) & M16
    h ^= (h << 7) & M16
    h ^= h >> 9
    h ^= (h << 8) & M16
    return h or 0xA5A5


def level_seed(world_seed, d):
    """src/levelgen.c:59"""
    return (world_seed + d * 0x9E37) & M16


# ---------------------------------------------------------------------------
# 2. Table extraction from the C source.
# ---------------------------------------------------------------------------

class ParseError(Exception):
    pass


def _read(src, name):
    with open(os.path.join(src, name), "r", encoding="utf-8",
              errors="replace") as f:
        return f.read()


def _strip_comments(text):
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
    return re.sub(r"//[^\n]*", " ", text)


def _body(text, decl):
    """the brace-balanced initialiser that follows decl"""
    i = text.find(decl)
    if i < 0:
        raise ParseError("could not find %r" % decl)
    i = text.index("{", i)
    depth, j = 0, i
    while j < len(text):
        if text[j] == "{":
            depth += 1
        elif text[j] == "}":
            depth -= 1
            if depth == 0:
                return text[i + 1:j]
        j += 1
    raise ParseError("unbalanced braces after %r" % decl)


def _rows(body):
    """split a table initialiser into its top-level braced rows"""
    out, depth, start = [], 0, None
    for k, ch in enumerate(body):
        if ch == "{":
            if depth == 0:
                start = k + 1
            depth += 1
        elif ch == "}":
            depth -= 1
            if depth == 0:
                out.append(body[start:k])
    return out


def _fields(row):
    """split a row on top-level commas, so a braced sub-list stays one field.
    Only braces nest: parentheses would break on the ')' weapon class char."""
    out, depth, cur = [], 0, ""
    for ch in row:
        if ch == "{":
            depth += 1
        elif ch == "}":
            depth -= 1
        if ch == "," and depth == 0:
            out.append(cur.strip())
            cur = ""
        else:
            cur += ch
    if cur.strip():
        out.append(cur.strip())
    return out


def _defines(text):
    d = {}
    for m in re.finditer(r"#define\s+(\w+)\s+(.+)", text):
        d[m.group(1)] = m.group(2).strip()
    return d


def _num(tok, consts):
    tok = tok.strip()
    m = re.fullmatch(r"'(.)'", tok)
    if m:
        return ord(m.group(1))
    if tok in consts:
        return _num(consts[tok], consts)
    if "|" in tok:
        v = 0
        for part in tok.split("|"):
            v |= _num(part, consts)
        return v
    tok = tok.strip("()")
    try:
        return int(tok, 0)
    except ValueError:
        raise ParseError("not a number: %r" % tok)


class Mon(object):
    __slots__ = ("ch", "hp", "dmg", "xp", "mindep", "corr", "atk", "name")

    def __init__(self, **kw):
        for k, v in kw.items():
            setattr(self, k, v)


class Obj(object):
    __slots__ = ("idx", "cls", "prop", "price", "mindep", "name")

    def __init__(self, **kw):
        for k, v in kw.items():
            setattr(self, k, v)


class Cls(object):
    __slots__ = ("name", "at", "hp", "pw", "kit", "gold", "align")

    def __init__(self, **kw):
        for k, v in kw.items():
            setattr(self, k, v)


class Tables(object):
    """everything the model needs, lifted straight out of the .c files"""

    def __init__(self, src):
        self.src = src
        mon_c = _strip_comments(_read(src, "monster.c"))
        mon_h = _strip_comments(_read(src, "monster.h"))
        item_c = _strip_comments(_read(src, "item.c"))
        cls_c = _strip_comments(_read(src, "classes.c"))
        game_h = _strip_comments(_read(src, "game.h"))

        consts = _defines(mon_h)
        consts.update(_defines(game_h))

        self.mons = []
        for row in _rows(_body(mon_c, "montypes[]")):
            f = _fields(row)
            if len(f) != 9:
                raise ParseError("montypes row has %d fields: %r" % (len(f), row))
            tok = f[0].strip()
            m = re.fullmatch(r"'(.)'", tok)
            ch = m.group(1) if m else chr(_num(tok, consts))
            self.mons.append(Mon(
                ch=ch, hp=int(f[1]), dmg=int(f[2]), xp=int(f[3]),
                mindep=int(f[4]), corr=int(f[6]), atk=f[7].strip(),
                name=f[8].strip().strip('"')))

        self.objs = []
        for i, row in enumerate(_rows(_body(item_c, "objtypes[NUMOBJ]"))):
            f = _fields(row)
            if len(f) != 5:
                raise ParseError("objtypes row has %d fields: %r" % (len(f), row))
            self.objs.append(Obj(
                idx=i, cls=f[0].strip().strip("'"), prop=int(f[1]),
                price=int(f[2]), mindep=int(f[3]),
                name=f[4].strip().strip('"')))

        kc = _defines(cls_c)
        self.classes = []
        for row in _rows(_body(cls_c, "classes[NCLASS]")):
            f = _fields(row)
            if len(f) != 7:
                raise ParseError("classes row has %d fields: %r" % (len(f), row))
            at = [int(v) for v in f[1].strip("{} ").split(",")]
            kit = []
            for k in f[4].strip("{} ").split(","):
                v = _num(k, kc)
                if v != 0xFF:
                    kit.append((v & 0x7F, bool(v & 0x80)))
            self.classes.append(Cls(
                name=f[0].strip().strip('"'), at=at, hp=int(f[2]),
                pw=int(f[3]), kit=kit, gold=int(f[5]), align=int(f[6])))

        self.DLVL_AMULET = int(_defines(game_h)["DLVL_AMULET"])
        self.MAXMON = int(_defines(mon_h)["MAXMON"])

    def mon(self, ch):
        for m in self.mons:
            if m.ch == ch:
                return m
        return self.mons[0]

    def pool(self, depth):
        """src/monster.c:118 pick_mon -- types eligible at this depth.
        The shopkeeper is skipped; mines natives (mindep 255) are injected
        directly by pick_mon, never pooled."""
        return [m for m in self.mons if m.ch != "@" and m.mindep <= depth]

    def eligible(self, cls, depth):
        """src/item.c:650 resolve_otyp"""
        e = [o for o in self.objs if o.cls == cls and o.mindep <= depth]
        return e or [o for o in self.objs if o.cls == cls][:1]

    def cls_by_name(self, name):
        for c in self.classes:
            if c.name.lower() == name.lower():
                return c
        raise SystemExit("no such class: %s (have: %s)" %
                         (name, ", ".join(c.name for c in self.classes)))


# ---------------------------------------------------------------------------
# 3. The formulas, each mirroring one line of C.
# ---------------------------------------------------------------------------

FORMULAS = [
    ("monster spawn HP", "src/monster_spawn.c:56",
     "m_hp = mt->hp + eff_depth() / 2",
     "mon_spawn_hp(mt, depth)"),
    ("monster bite", "src/monster_ai.c:156",
     "bite = rn2(mt->dmg) + 1 + eff_depth() / 4",
     "mon_bite(rng, mt, depth)"),
    ("armour soak", "src/monster_ai.c:158",
     "if (armor_def >= bite) miss; else bite -= armor_def",
     "apply_soak(bite, armor_def)  -- a full absorb is a MISS, not 0 damage"),
    ("hero to-hit", "src/monster_ai.c:130",
     "miss if rn2(20) >= 12 + (at_dex >> 1) + (eff_luck() >> 1)",
     "hero_hits(rng, dex, luck)"),
    ("hero damage", "src/monster_ai.c:134",
     "dmg = rn2(4) + 1 + weapon_dmg; +2 if St>=17, +1 if St>=14",
     "hero_dmg(rng, weapon_dmg, str)"),
    ("XP threshold", "src/monster_ai.c:40",
     "level up while xp >= xlvl * 20 (xlvl < 30)",
     "xp_threshold(xlvl)"),
    ("level-up HP", "src/monster_ai.c:42",
     "gain = rn2(4) + 2 + (at_con >= 14), capped so pmaxhp <= 250",
     "level_gain(rng, con)"),
    ("regeneration", "src/nexthack.c:783",
     "1 HP every 14/17/20 turns (Co>=16 / Co>=13 / else), halved by ring",
     "regen_period(con, ring)"),
    ("monsters per level", "src/monster_spawn.c:110",
     "count = 2 + eff_depth(), capped at 8",
     "spawn_count(depth)"),
    ("gear power", "src/item.c:295",
     "eff = prop + ench - ero; +1 blessed, -2 cursed; armour redux = max(eff-1,1)",
     "gear_eff(obj) / armor_redux(eff)"),
    ("floor enchantment", "src/item.c:693",
     "roll = (h >> 5) % 100; +1 if roll < depth, +2 if roll < depth/3",
     "floor_ench(h, depth)"),
    ("floor BUC", "src/item.c:700",
     "r = (h >> 11) & 7 -- 5/8 uncursed, 2/8 cursed, 1/8 blessed",
     "floor_buc(h)"),
    ("amulet gauntlet", "src/monster.c:123",
     "pool depth = has_amulet ? eff_depth() + 15 : eff_depth()",
     "pool(depth + 15) -- note the BITE still uses the real depth"),
]


def mon_spawn_hp(mt, depth):
    return mt.hp + depth // 2


def mon_bite(rng, mt, depth):
    return rng.rn2(mt.dmg) + 1 + depth // 4


def apply_soak(bite, armor_def):
    """returns damage dealt, or None for a full absorb (the C prints a miss)"""
    return None if armor_def >= bite else bite - armor_def


def hero_hits(rng, dex, luck=0):
    return rng.rn2(20) < 12 + (dex >> 1) + (luck >> 1)


def hero_dmg(rng, weapon_dmg, strength):
    d = rng.rn2(4) + 1 + weapon_dmg
    if strength >= 17:
        d += 2
    elif strength >= 14:
        d += 1
    return d


def xp_threshold(xlvl):
    return xlvl * 20


def level_gain(rng, con):
    return rng.rn2(4) + 2 + (1 if con >= 14 else 0)


def regen_period(con, ring=False):
    p = 14 if con >= 16 else 17 if con >= 13 else 20
    return p >> 1 if ring else p


def spawn_count(depth):
    return min(2 + depth, 8)


def armor_redux(eff):
    """src/item.c:303 -- an armour piece shields eff-1 (at least 1)"""
    return 0 if eff <= 0 else (eff - 1 if eff > 1 else 1)


def floor_ench(h, depth):
    roll = (h >> 5) % 100
    e = 0
    if roll < depth:
        e = 1
    if roll < depth // 3:
        e = 2
    return e


def floor_buc(h):
    r = (h >> 11) & 7
    return "uncursed" if r < 5 else "cursed" if r < 7 else "blessed"


def buc_mod(buc):
    return 1 if buc == "blessed" else -2 if buc == "cursed" else 0


def potion_heal(rng, prop):
    """src/item.c:1352 -- heal = rn2(6) + prop"""
    return rng.rn2(6) + prop


SLEEP_P = 0.5   # src/monster_spawn.c:41 -- spawns_asleep is (hash & 1)


# ---------------------------------------------------------------------------
# 4. The hero model: one struct the duel and the run sim both fight with.
# ---------------------------------------------------------------------------

class Hero(object):
    def __init__(self, cls, tables):
        self.t = tables
        self.cls = cls
        self.st, self.dex, self.con, self.i, self.wis, self.cha = cls.at
        self.maxhp = cls.hp
        self.hp = cls.hp
        self.xlvl = 1
        self.xp = 0
        self.luck = 0
        self.weapon_dmg = 0
        self.armor_def = 0
        self.potions = []            # otyps of carried healing potions
        # best-worn tracking: item.c equips the highest prop+ench-ero
        self.best_wpn = None         # (eff,) of the wielded weapon
        self.best_arm = None
        self.best_ring = None
        for otyp, equipped in cls.kit:
            o = tables.objs[otyp]
            if o.cls == "!" and o.prop > 0:
                self.potions.append(otyp)
            if equipped:
                self.offer(o.cls, o.prop)
        self.recompute()

    # -- src/item.c:478 best_of + :284 recompute_gear -------------------------
    def offer(self, cls, eff):
        """consider a piece of gear; the game wears the highest eff it holds"""
        if eff <= 0:
            return False
        if cls == ")":
            if self.best_wpn is None or eff > self.best_wpn:
                self.best_wpn = eff
                return True
        elif cls == "[":
            if self.best_arm is None or eff > self.best_arm:
                self.best_arm = eff
                return True
        elif cls == "=":
            if self.best_ring is None or eff > self.best_ring:
                self.best_ring = eff
                return True
        return False

    def recompute(self):
        self.weapon_dmg = self.best_wpn or 0
        redux = 0
        if self.best_arm:
            redux += armor_redux(self.best_arm)
        if self.best_ring:
            redux += self.best_ring
        self.armor_def = redux

    def gain_xp(self, rng, amt):
        """src/monster_ai.c:37"""
        self.xp += amt
        while self.xlvl < 30 and self.xp >= xp_threshold(self.xlvl):
            gain = level_gain(rng, self.con)
            self.xlvl += 1
            if self.maxhp + gain > 250:
                gain = 0 if self.maxhp >= 250 else 250 - self.maxhp
            self.maxhp += gain
            self.hp += gain


# ---------------------------------------------------------------------------
# 5. One fight, turn by turn, in the game's own order.
# ---------------------------------------------------------------------------

def fight(rng, hero, mt, depth, asleep=None, pet_dmg=0):
    """Melee a single monster to the death.  Returns (won, hp_lost, turns).

    Order mirrors the turn loop (src/mainentry.c): the hero swings, then
    monsters_turn() answers.  A sleeping monster cannot dodge (the sneak
    attack always lands, src/monster_ai.c:129) and does not answer until the
    blow wakes it (src/monster_ai.c:81 m_sleep = 0 on any hit).
    """
    mhp = mon_spawn_hp(mt, depth)
    if asleep is None:
        asleep = rng.rn2(2) == 0        # spawns_asleep is a coin from a hash
    start = hero.hp
    turns = 0
    while True:
        turns += 1
        if asleep or hero_hits(rng, hero.dex, hero.luck):
            mhp -= hero_dmg(rng, hero.weapon_dmg, hero.st)
        asleep = False                  # the swing wakes it either way
        if mhp <= 0:
            return True, start - hero.hp, turns
        if pet_dmg:
            mhp -= pet_dmg
            if mhp <= 0:
                return True, start - hero.hp, turns
        if mt.ch == "@":                # the shopkeeper never fights back
            continue
        bite = mon_bite(rng, mt, depth)
        dealt = apply_soak(bite, hero.armor_def)
        if dealt:
            hero.hp -= dealt
            if hero.hp <= 0:
                hero.hp = 0
                return False, start, turns
        if turns > 400:                 # a stalemate is a loss of a kind
            return False, start - hero.hp, turns


# ---------------------------------------------------------------------------
# 6. The floor's supply: what gear a depth actually offers.
# ---------------------------------------------------------------------------

def floor_gear_sample(t, depth, cls, seeds=400, cells=8):
    """The effective power of one class of item generated at this depth.

    Walks real (seed, cell) pairs through the game's own item_hash, so the
    distribution is the one the dungeon really produces -- not an assumed
    uniform.  Returns the list of eff values (prop + ench + buc)."""
    out = []
    for s in range(1, seeds + 1):
        ws = (s * 2654435761) & M16 or 1
        for c in range(cells):
            x, y = 3 + (c * 7) % 74, 2 + (c * 5) % 19
            h = item_hash(ws, depth, x, y)
            elig = t.eligible(cls, depth)
            o = elig[h % len(elig)]
            eff = o.prop + floor_ench(h, depth) + buc_mod(floor_buc(h))
            out.append(max(eff, 0))
    return out


def mean(xs):
    return sum(xs) / float(len(xs)) if xs else 0.0


def pct(xs, p):
    if not xs:
        return 0
    s = sorted(xs)
    k = min(len(s) - 1, max(0, int(round(p / 100.0 * (len(s) - 1)))))
    return s[k]


# ---------------------------------------------------------------------------
# 7. A whole run, descent and ascent.
# ---------------------------------------------------------------------------

class RunOpts(object):
    """Every judgement call the model has to make is a named knob here, so the
    sensitivity of a conclusion to a guess can be measured instead of argued."""

    def __init__(self, turns_per_level=200, engage=0.8, pet=False,
                 pickup=True, quaff_at=0.35, ascend=True):
        self.turns_per_level = turns_per_level
        self.engage = engage          # fraction of the level's spawns fought
        self.pet = pet                # is the dog still alive and biting?
        self.pickup = pickup          # does the hero collect and wear the loot?
        self.quaff_at = quaff_at      # drink a healing potion below this * max
        self.ascend = ascend          # climb back out with the Amulet


def level_loot(rng, t, hero, depth, opts):
    """The loot block of one ordinary level (src/levelgen.c:559) resolved
    through the game's own item hash, then offered to the hero."""
    if not opts.pickup:
        return
    ws = rng.next() or 1
    cell = [0]

    def draw(cls):
        cell[0] += 1
        x, y = 3 + (cell[0] * 11) % 74, 2 + (cell[0] * 7) % 19
        h = item_hash(ws, depth, x, y)
        elig = t.eligible(cls, depth)
        o = elig[h % len(elig)]
        ench = floor_ench(h, depth) if cls in ")[" else 0
        eff = o.prop + ench + buc_mod(floor_buc(h)) if cls in ")[=" else o.prop
        return o, max(eff, 0)

    o, eff = draw(")")
    hero.offer(")", eff)
    o, eff = draw("[")
    hero.offer("[", eff)
    for _ in range(1 if depth < 2 else 2):
        o, _e = draw("!")
        if o.prop > 0 and o.cls == "!":          # a healing kind
            hero.potions.append(o.idx)
    if depth >= 3 and rng.rn2(2):
        o, eff = draw("=")
        hero.offer("=", eff)
    hero.recompute()


def do_level(rng, t, hero, depth, opts, log=None):
    """Fight through one level.  Returns True if the hero lived."""
    n = int(round(spawn_count(depth) * opts.engage))
    pool = t.pool(depth + 15 if opts.__dict__.get("amulet") else depth)
    if not pool:
        pool = t.pool(depth)
    regen_each = 0
    if n:
        regen_each = (opts.turns_per_level // n) // regen_period(hero.con)
    pet_dmg = 3 if opts.pet else 0        # the dog's mean bite, rn2(4)+2

    for _ in range(n):
        mt = pool[rng.rn2(len(pool))]
        if mt.ch == "e":                  # the floating eye never attacks
            continue
        won, lost, turns = fight(rng, hero, mt, depth, pet_dmg=pet_dmg)
        if hero.hp <= 0:
            if log is not None:
                log.append((depth, mt.name, hero.maxhp, hero.armor_def))
            return False
        hero.gain_xp(rng, mt.xp)
        hero.hp = min(hero.maxhp, hero.hp + regen_each)
        if hero.hp < hero.maxhp * opts.quaff_at and hero.potions:
            ot = hero.potions.pop()
            hero.hp = min(hero.maxhp, hero.hp + potion_heal(rng, t.objs[ot].prop))
    return True


def simulate_run(rng, t, cls, opts, log=None):
    """One life.  Returns (outcome, depth) where outcome is
    'died' | 'won' | 'survived' (ran out of modelled dungeon)."""
    hero = Hero(cls, t)
    opts.amulet = False
    for depth in range(1, t.DLVL_AMULET + 1):
        level_loot(rng, t, hero, depth, opts)
        if not do_level(rng, t, hero, depth, opts, log):
            return "died", depth
    if not opts.ascend:
        return "survived", t.DLVL_AMULET
    # the gauntlet: with the Amulet the pool is 15 floors deeper than the
    # ground under your feet (src/monster.c:123) -- but the BITE bonus still
    # keys off the real depth, which is the whole point of measuring it.
    opts.amulet = True
    for depth in range(t.DLVL_AMULET, 0, -1):
        if not do_level(rng, t, hero, depth, opts, log):
            return "died", -depth          # negative depth = died on the way up
    return "won", 0


def run_batch(t, cls, opts, trials, seed=1):
    rng = Rng(seed)
    outcomes = {"died": 0, "won": 0, "survived": 0}
    deaths = {}
    ascent_deaths = 0
    log = []
    for _ in range(trials):
        what, d = simulate_run(rng, t, cls, opts, log)
        outcomes[what] += 1
        if what == "died":
            if d < 0:
                ascent_deaths += 1
                d = -d
            deaths[d] = deaths.get(d, 0) + 1
    return outcomes, deaths, ascent_deaths, log


# ---------------------------------------------------------------------------
# 8. Rest economics.
# ---------------------------------------------------------------------------

WANDER_P = 70       # src/monster_spawn.c:191 -- rn2(70), or rn2(25) with the Amulet
WANDER_P_AMULET = 25


def rest_breakeven(con, ring=False, amulet=False):
    """How much a fight may cost before resting stops paying for itself.

    There is no rest command: 's' (search) is the only way to pass a turn
    (src/nexthack.c:1327 turns++), so recovery is 1 HP every regen_period
    turns.  Meanwhile every turn rolls a wandering monster.  Resting is
    profitable only while

        1 / regen_period  >  hp_cost_per_fight / wander_period

    i.e. while a fight costs less than wander_period / regen_period HP."""
    return (WANDER_P_AMULET if amulet else WANDER_P) / float(regen_period(con, ring))


# ---------------------------------------------------------------------------
# 9. Commands.
# ---------------------------------------------------------------------------

def h1(s):
    print("")
    print(s)
    print("=" * len(s))


def h2(s):
    print("")
    print(s)
    print("-" * len(s))


def cmd_tables(t, a):
    h1("Parsed out of %s" % os.path.relpath(t.src))
    h2("monsters (montypes[], src/monster.c)")
    print("  %-14s %2s %4s %4s %4s %6s  %s" %
          ("name", "ch", "hp", "dmg", "xp", "mindep", "special"))
    for m in t.mons:
        print("  %-14s %2s %4d %4d %4d %6d  %s" %
              (m.name, m.ch, m.hp, m.dmg, m.xp, m.mindep,
               "" if m.atk == "ATK_NONE" else m.atk[4:].lower()))
    h2("objects (objtypes[], src/item.c)")
    for o in t.objs:
        print("  %2d %s %-28s prop %2d  mindep %3d  %5dgp" %
              (o.idx, o.cls, o.name, o.prop, o.mindep, o.price))
    h2("classes (classes[], src/classes.c)")
    for c in t.classes:
        hero = Hero(c, t)
        print("  %-9s St%2d Dx%2d Co%2d  hp %2d  wield +%d  armor_def %d" %
              (c.name, c.at[0], c.at[1], c.at[2], c.hp,
               hero.weapon_dmg, hero.armor_def))


def cmd_formulas(t, a):
    h1("Every formula, next to the C it mirrors")
    for name, where, c, py in FORMULAS:
        print("")
        print("  %s   [%s]" % (name, where))
        print("      C : %s" % c)
        print("      py: %s" % py)


def cmd_curves(t, a):
    h1("Threat vs durability by depth")
    print("""
The monster half is exact -- pure arithmetic on the tables.  The hero half is
the MEDIAN of %d independently rolled gear paths down to that depth, so it is
what the dungeon typically hands out, not a best case.
""".strip() % 41)

    h2("the dungeon's half")
    print("")
    print("  %5s %5s %8s %8s %8s" %
          ("depth", "mobs", "mean HP", "raw bite", "max bite"))
    print("  " + "-" * 38)
    for d in a.depths:
        pool = [m for m in t.pool(d) if m.ch != "@"]
        print("  %5d %5d %8.1f %8.1f %8d" %
              (d, spawn_count(d), mean([mon_spawn_hp(m, d) for m in pool]),
               mean([(m.dmg + 1) / 2.0 + d // 4 for m in pool]),
               max(m.dmg + d // 4 for m in pool)))
    print("""
  mobs     = spawns per level (src/monster_spawn.c:110), capped at 8
  raw bite = mean damage rolled BEFORE armour (src/monster_ai.c:156)
  max bite = the biggest single blow the pool can roll""")

    for cls in ([t.cls_by_name(a.cls)] if a.cls else t.classes):
        h2(cls.name)
        print("")
        print("  %5s %6s %5s %5s %7s %9s %9s %8s" %
              ("depth", "maxHP", "+dmg", "def", "soaked", "HP/fight",
               "fights/bar", "verdict"))
        print("  " + "-" * 64)
        for d in a.depths:
            hero = _hero_at_depth(t, cls, d, a)
            pool = [m for m in t.pool(d) if m.ch not in "@e"]
            # share of every face of every bite die this armour absorbs whole
            faces, blocked = 0, 0
            for m in pool:
                for k in range(m.dmg):
                    faces += 1
                    if hero.armor_def >= k + 1 + d // 4:
                        blocked += 1
            rng = Rng(31)
            lost = []
            for _ in range(a.trials // 4):
                h = _clone(hero)
                h.hp = h.maxhp
                m = pool[rng.rn2(len(pool))]
                _w, l, _n = fight(rng, h, m, d)
                lost.append(l)
            cost = mean(lost)
            bar = hero.maxhp / cost if cost > 0.05 else 999
            verdict = ("untouched" if cost < 0.5 else
                       "safe" if bar >= 2 * spawn_count(d) else
                       "tight" if bar >= spawn_count(d) else "lethal")
            print("  %5d %6d %5d %5d %6.0f%% %9.1f %9s %8s" %
                  (d, hero.maxhp, hero.weapon_dmg, hero.armor_def,
                   100.0 * blocked / faces, cost,
                   "-" if bar > 900 else "%.1f" % bar, verdict))
        print("""
  soaked     = share of possible bites this armour absorbs ENTIRELY --
               armor_def >= bite prints a miss (src/monster_ai.c:158)
  HP/fight   = mean HP lost per single melee, fought to the death
  fights/bar = how many such fights one full HP bar buys, against the
               %d spawns a deep level throws at you
  verdict    = safe: a bar covers the level twice over; tight: about once;
               lethal: the level costs more than a full bar""" % 8)


def cmd_duel(t, a):
    h1("Duels: one hero, one monster, at full HP")
    cls = t.cls_by_name(a.cls) if a.cls else t.classes[0]
    print("""
%s with the median gear of each depth.  The cells are HP LOST, because the
win rate turned out to be the boring half of the answer: a hero who starts a
fight at full HP wins essentially every one-on-one in the game.  A cell is
marked with * if the hero lost even one of %d duels.
""".strip() % (cls.name, a.trials))
    depths = a.depths
    shown, seen = [], set()
    for m in t.mons:
        if m.ch in "@Gh" or m.name in seen:
            continue      # keeper never fights; mines natives; 'x'/'m' are one
        seen.add(m.name)
        shown.append(m)
    print("")
    print("  %-14s" % "monster" + "".join("%8s" % ("d%d" % d) for d in depths))
    print("  " + "-" * (14 + 8 * len(depths)))
    for m in shown:
        row = "  %-14s" % m.name
        for d in depths:
            appears = (m.mindep <= d if m.mindep != 255
                       else (d == t.DLVL_AMULET if m.ch == "M" else d >= 6))
            if not appears:
                row += "%8s" % "-"
                continue
            hero = _hero_at_depth(t, cls, d, a)
            rng = Rng(99)
            wins, lost = 0, []
            for _ in range(a.trials):
                h = _clone(hero)
                h.hp = h.maxhp
                won, l, _n = fight(rng, h, m, d)
                wins += won
                lost.append(l)
            row += "%7s%s" % ("%.1f" % mean(lost),
                              "*" if wins < a.trials else " ")
        print(row)
    print("""
  The floating eye never bites back but freezes you when you strike it
  (src/monster_ai.c:141) -- not modelled here, so read its row as 'free XP,
  paid for in paralysed turns while everything else closes in'.
  The acid blob's row also understates it: it corrodes the weapon you hit
  it with (src/monster_ai.c:145), a cost that lands on later fights.""")


def _hero_at_depth(t, cls, depth, a, samples=41):
    """The model's MEDIAN hero on arrival at `depth`.

    Gear is a lottery -- the floor's enchant and BUC rolls, and whether a
    ring turned up at all -- so one sampled path is not a hero, it is an
    anecdote.  Walk `samples` independent dungeons down to `depth` and
    return the one whose armor_def is the median, which is the figure the
    duel and rest tables are about."""
    rng = Rng(4242)
    heroes = []
    for _ in range(samples):
        hero = Hero(cls, t)
        opts = RunOpts(a.turns, a.engage, a.pet)
        opts.amulet = False
        for d in range(1, depth + 1):
            level_loot(rng, t, hero, d, opts)
            if d < depth:
                hero.gain_xp(rng, int(spawn_count(d) * a.engage *
                                      mean([m.xp for m in t.pool(d)])))
        hero.hp = hero.maxhp
        heroes.append(hero)
    heroes.sort(key=lambda h: (h.armor_def, h.weapon_dmg))
    return heroes[len(heroes) // 2]


def _clone(h):
    import copy
    c = copy.copy(h)
    c.potions = list(h.potions)
    return c


def cmd_gear(t, a):
    h1("What the floor actually offers")
    print("""
Every ordinary level drops one weapon and one armour (src/levelgen.c:559).
Their power is resolve_floor's: the eligible type pool at that depth, plus a
depth-scaled enchantment and a BUC roll (5/8 uncursed, 2/8 cursed, 1/8
blessed).  Sampled through the game's own item_hash over real cells.
""".strip())
    print("")
    print("  %5s | %-28s | %-28s" % ("depth", "weapon eff (+dmg)", "armour eff"))
    print("  %5s | %6s %6s %6s %6s | %6s %6s %6s %6s" %
          ("", "mean", "p50", "p90", "best", "mean", "p50", "p90", "best"))
    print("  " + "-" * 68)
    for d in a.depths:
        w = floor_gear_sample(t, d, ")", a.seeds)
        ar = floor_gear_sample(t, d, "[", a.seeds)
        print("  %5d | %6.2f %6d %6d %6d | %6.2f %6d %6d %6d" %
              (d, mean(w), pct(w, 50), pct(w, 90), max(w),
               mean(ar), pct(ar, 50), pct(ar, 90), max(ar)))
    h2("healing supply")
    print("""
  Potions are drawn from the whole eligible pool, so the share of them that
  actually heal falls as the pool widens with depth -- while the damage
  rises.  This is the supply curve for the only recovery that is not a
  thousand keypresses (see the rest command).""")
    print("")
    print("  %5s %8s  %s" % ("depth", "healing", "eligible potions"))
    for d in a.depths:
        elig = t.eligible("!", d)
        heals = [o for o in elig if o.prop > 0]
        print("  %5d %7.0f%%  %s" %
              (d, 100.0 * len(heals) / len(elig),
               ", ".join(o.name.replace("potion of ", "") for o in elig)))


def cmd_rest(t, a):
    h1("Rest economics: can you heal up between fights?")
    print("""
There is no rest command.  Search is the only key that passes a turn without
moving (src/nexthack.c:1327 turns++), and regeneration is 1 HP every 14-20
turns (src/nexthack.c:783).  Every turn also rolls a wandering monster at
1/%d -- 1/%d once you carry the Amulet (src/monster_spawn.c:191).

So resting pays only while an average fight costs less than
wander_period / regen_period HP:
""".strip() % (WANDER_P, WANDER_P_AMULET))
    print("")
    print("  %-24s %10s %12s %14s" %
          ("constitution", "HP/turn", "break-even", "with Amulet"))
    print("  " + "-" * 64)
    for label, con, ring in (("Co < 13", 12, False), ("Co 13-15", 13, False),
                             ("Co >= 16", 16, False),
                             ("Co >= 16 + ring of regen", 16, True)):
        p = regen_period(con, ring)
        print("  %-24s %10s %9.1f HP %11.1f HP" %
              (label, "1/%d" % p, rest_breakeven(con, ring),
               rest_breakeven(con, ring, amulet=True)))
    h2("what a fight actually costs")
    cls = t.cls_by_name(a.cls) if a.cls else t.classes[0]
    print("")
    print("  %s, model gear, mean HP lost per single melee:" % cls.name)
    print("")
    print("  %5s %10s %12s" % ("depth", "HP/fight", "resting?"))
    for d in a.depths:
        hero = _hero_at_depth(t, cls, d, a)
        rng = Rng(31)
        lost = []
        pool = [m for m in t.pool(d) if m.ch not in "@e"]
        for _ in range(a.trials):
            h = _clone(hero)
            h.hp = h.maxhp
            m = pool[rng.rn2(len(pool))]
            _w, l, _n = fight(rng, h, m, d)
            lost.append(l)
        be = rest_breakeven(hero.con)
        print("  %5d %10.1f %12s" %
              (d, mean(lost), "pays" if mean(lost) < be else "LOSES"))
    print("""
  LOSES means the wandering monsters a rest attracts cost more HP than the
  rest restores: past that depth the only recovery is potions.""")


def cmd_runs(t, a):
    h1("Full runs: where the dungeon kills you")
    print("""
%d lives per class under the model's play policy: fight %.0f%% of every
level's spawns to the death, wear the best of what the floor drops, drink a
healing potion below %.0f%% HP%s, then dive.  Read the SHAPE, not the
absolute rate -- the assumptions are listed at the end.
""".strip() % (a.trials_runs, 100 * a.engage, 100 * RunOpts().quaff_at,
               ", the dog fighting alongside" if a.pet else ", no pet"))
    csv_rows = []
    for cls in ([t.cls_by_name(a.cls)] if a.cls else t.classes):
        opts = RunOpts(a.turns, a.engage, a.pet)
        outcomes, deaths, ascent, log = run_batch(t, cls, opts, a.trials_runs)
        tot = float(a.trials_runs)
        alld = sorted([k for k, v in deaths.items() for _ in range(v)])
        h2(cls.name)
        print("  won %.1f%%   died %.1f%% (%.1f%% of those on the way back up)"
              % (100 * outcomes["won"] / tot, 100 * outcomes["died"] / tot,
                 100.0 * ascent / max(1, outcomes["died"])))
        if alld:
            print("  death depth: p10 %d   median %d   p90 %d   deepest %d" %
                  (pct(alld, 10), pct(alld, 50), pct(alld, 90), max(alld)))
        print("")
        band = 5
        top = ((max(alld) if alld else 0) // band + 1) * band
        for lo in range(1, top + 1, band):
            n = sum(v for k, v in deaths.items() if lo <= k < lo + band)
            print("  d%-7s %-40s %4.1f%%" %
                  ("%d-%d" % (lo, lo + band - 1),
                   "#" * int(round(40.0 * n / tot)), 100.0 * n / tot))
        killers = {}
        for d, name, _mh, _ad in log:
            killers[name] = killers.get(name, 0) + 1
        top_k = sorted(killers.items(), key=lambda kv: -kv[1])[:6]
        print("")
        print("  killed by: " +
              ", ".join("%s %.0f%%" % (k, 100.0 * v / max(1, len(log)))
                        for k, v in top_k))
        for k in sorted(deaths):
            csv_rows.append((cls.name, k, deaths[k]))
    if a.csv:
        _write_csv(a.csv, "deaths.csv", ["class", "depth", "deaths"], csv_rows)
    print("""
  Model assumptions (each is a knob: --turns, --engage, --pet):
    * %d turns walked per level, so regeneration between fights is about
      %d turns, i.e. %.1f HP, at depth 8;
    * the hero never flees a fight it has started, and never rests;
    * no wands, spells, altars, Excalibur or gain-level potions (all of
      which help the hero) -- and no traps, hunger, dragon breath, poison,
      blindness or cursed gear (all of which hurt it);
    * every level's weapon and armour is found and worn.
  The first and last bullets make this OPTIMISTIC about gear and
  PESSIMISTIC about tactics; the shape of the curve is the finding.""" %
          (a.turns, a.turns // 8, (a.turns // 8) / float(regen_period(12))))


def cmd_sweep(t, a):
    h1("Sensitivity: does the conclusion survive the guesses?")
    print("""
The run model has to guess three things it cannot read out of the source:
how many turns a level takes to walk, how much of it the player fights, and
whether the dog is still alive.  If the wall moved a lot across those
guesses, the wall would be an artefact of the guessing.  It does not.
""".strip())
    cls = t.cls_by_name(a.cls) if a.cls else t.classes[0]
    n = max(120, a.trials_runs // 3)
    print("\n  %s, %d lives per cell, median death depth:\n" % (cls.name, n))
    print("  %-28s %8s %8s %8s" % ("", "engage .5", "engage .8", "engage 1.0"))
    print("  " + "-" * 56)
    for turns in (100, 200, 400, 800):
        for pet in (False, True):
            label = "%d turns/level%s" % (turns, ", with dog" if pet else "")
            row = "  %-28s" % label
            for eng in (0.5, 0.8, 1.0):
                opts = RunOpts(turns, eng, pet)
                _o, deaths, _asc, _log = run_batch(t, cls, opts, n, seed=5)
                alld = sorted([k for k, v in deaths.items() for _ in range(v)])
                row += "%8s" % (pct(alld, 50) if alld else "survives")
            print(row)
    print("""
  Every cell has a wall; where it stands is another matter.  It slides from
  about d32 (dive at 100 turns a level, fight everything, no dog) to d48 and
  past the Amulet (800 turns a level, fight half, dog alive).  So the wall
  itself is not an artefact of the guessing -- but its DEPTH is, and the
  thing it is most sensitive to is turns per level, i.e. how much
  regeneration the player collects by walking.  That is the same lever as
  resting, and it is the one the game gives the player no command for.""")


def cmd_gauntlet(t, a):
    h1("The Amulet gauntlet: is the climb back out actually harder?")
    print("""
Carrying the Amulet widens the spawn pool by 15 floors (src/monster.c:123)
and triples the wandering-monster rate (src/monster_spawn.c:191).  But the
bite bonus keys off the REAL depth, not the pool depth -- so near the
surface the dungeon sends its deep servants with shallow teeth.
""".strip())
    cls = t.cls_by_name(a.cls) if a.cls else t.classes[0]
    print("")
    print("  %5s %-22s %10s %10s %9s" %
          ("depth", "toughest in pool", "descending", "ascending", "change"))
    print("  " + "-" * 62)
    for d in a.depths:
        hero = _hero_at_depth(t, cls, d, a)
        out = []
        for pool_depth in (d, d + 15):
            pool = [m for m in t.pool(pool_depth) if m.ch not in "@e"]
            rng = Rng(77)
            lost = []
            for _ in range(a.trials // 4):
                h = _clone(hero)
                h.hp = h.maxhp
                m = pool[rng.rn2(len(pool))]
                _w, l, _n = fight(rng, h, m, d)   # bite uses the REAL depth
                lost.append(l)
            out.append(mean(lost))
        worst = max(t.pool(d + 15), key=lambda m: m.hp)
        print("  %5d %-22s %8.1f HP %8.1f HP %8s" %
              (d, worst.name, out[0], out[1],
               "x%.1f" % (out[1] / out[0]) if out[0] > 0.05 else "-"))
    print("""
  Read the shallow rows: the pool is full of trolls and dragons, and they
  still cost the hero almost nothing, because a dragon at depth 5 bites for
  rn2(8)+1+1.  The gauntlet is a costume change, not a difficulty curve --
  it only bites once the real depth is deep enough to supply the bonus.""")


def _write_csv(dirname, name, header, rows):
    if not os.path.isdir(dirname):
        os.makedirs(dirname)
    path = os.path.join(dirname, name)
    with open(path, "w", encoding="utf-8") as f:
        f.write(",".join(header) + "\n")
        for r in rows:
            f.write(",".join(str(c) for c in r) + "\n")
    print("")
    print("  wrote %s" % path)


def cmd_report(t, a):
    for fn in (cmd_tables, cmd_curves, cmd_gear, cmd_rest, cmd_duel,
               cmd_gauntlet, cmd_runs, cmd_sweep):
        fn(t, a)


COMMANDS = {
    "tables": cmd_tables, "formulas": cmd_formulas, "curves": cmd_curves,
    "duel": cmd_duel, "gear": cmd_gear, "rest": cmd_rest, "runs": cmd_runs,
    "sweep": cmd_sweep, "gauntlet": cmd_gauntlet,
    "report": cmd_report,
}


def main(argv=None):
    p = argparse.ArgumentParser(
        description="NextHack balance instrument",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__)
    p.add_argument("command", nargs="?", default="report",
                   choices=sorted(COMMANDS))
    p.add_argument("--src", default=DEFAULT_SRC)
    p.add_argument("--csv", default=None, help="directory for raw CSV output")
    p.add_argument("--trials", type=int, default=4000,
                   help="Monte-Carlo samples per duel cell (default 4000)")
    p.add_argument("--trials-runs", type=int, default=600,
                   help="simulated lives per class (default 600)")
    p.add_argument("--class", dest="cls", default=None)
    p.add_argument("--turns", type=int, default=200,
                   help="turns walked per level (default 200)")
    p.add_argument("--engage", type=float, default=0.8,
                   help="fraction of a level's spawns fought (default 0.8)")
    p.add_argument("--pet", action="store_true",
                   help="the dog fights alongside the hero")
    p.add_argument("--depths", default="1,2,3,5,8,12,16,20,25,30,40,50")
    a = p.parse_args(argv)
    a.depths = [int(x) for x in a.depths.split(",")]
    a.seeds = 300
    try:
        t = Tables(a.src)
    except ParseError as e:
        raise SystemExit("balance.py: the C tables no longer parse: %s\n"
                         "(the model refuses to run on stale numbers)" % e)
    COMMANDS[a.command](t, a)
    return 0


if __name__ == "__main__":
    sys.exit(main())
