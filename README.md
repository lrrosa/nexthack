# NextHack — a NetHack-inspired roguelike for the ZX Spectrum Next

A from-scratch NetHack-style roguelike for the
[ZX Spectrum Next](https://www.specnext.com/), built with **Z88DK** (the
`zsdcc`/SDCC C compiler) and tested on the **CSpect** emulator.

## Strategy

This is a **fresh reimplementation** of NetHack's design in C, sized for the Z80N
— not a recompile of NetHack's source. The design reference is the NetHack **5.0
development version** (the branch long known as 3.7; the latest *stable* release is
3.6.7). You cannot just
compile the original: it is ~250k lines of C that assume 32-bit ints and
megabytes of flat RAM (5.0 even depends on Lua), while the Z80 only sees 64 KB at
a time. So the engine is rebuilt on a dedicated **Next platform layer** (display,
keyboard, sound), reusing NetHack's design, key bindings and feel rather than its
code.

## Status

- [x] **Phase 0 — Toolchain** validated: `zcc` compiles C → a valid `.nex`.
- [x] **Phase 1 — Vertical slice**: a NetHack-style level (2 rooms + corridor),
      hero `@` moving with NetHack's vi-keys, wall collision, decorative
      monsters/items/stairs.
- [x] **Phase 1b — 80-column display** ([nexthack.c](nexthack.c)): rendered on the
      Next's hardware **tilemap** (80×32), matching NetHack's native map width.
- [x] **Phase 2** — Procedural level generation: random rooms on a grid of
      sectors, corridors with doors, up/down stairs (`>` descends to a new
      level), scattered gold/food/monsters. WASD + arrow-key movement.
- [x] **Phase 3** — Graphics & interaction: colourful 8x8 pixel-art tiles for
      the whole map; gold pickup; bump-to-attack combat with monster/player HP,
      retaliation and death/restart; hold-a-key continuous movement.
- [x] **Phase 4** — Level persistence: each Dlvl is regenerated deterministically
      from a per-depth seed (same depth = same map), with collected gold and
      killed monsters remembered. Title screen seeds the world from the player's
      reaction time for good variety.
- [x] **Phase 5** — Monster AI: monsters chase the hero each turn (greedy
      pathing) and attack when adjacent; turn-based combat (you strike on your
      turn, they strike back on theirs).
- [x] **Phase 6** — Field of view (fog of war): the hero's room lights up,
      corridors reveal around the hero, explored terrain is remembered, and
      monsters only show while in view.
- [x] **Phase 7** — Inventory and items: weapons, armor, potions and food on
      the floor; pick up, inventory screen, wield/wear/quaff/eat. Corridors now
      leave rooms through a single edge door and run through the rock.
- [x] **Phase 8** — Survival & persistence: hunger and slow HP regeneration;
      rooms joined to grid-adjacent neighbours (no spurious doors); per-level
      fog-of-war memory; gold, monster kills and item pickups all remembered
      across revisits.
- [x] **Phase 9** — Bestiary & progression: several monster types (rat, bat,
      kobold, dog, snake, orc, zombie) with their own tiles and stats, spawned
      by depth; monster HP/damage scale with depth; XP and experience levels
      (kills raise max HP); stairs placed in random rooms.
- [x] **Phase 10** — Sound: beeper effects for hits, kills, taking damage,
      picking up gold (a distinct coin sound) and items, drinking, eating,
      stairs, levelling up and dying (CPU drops to 3.5 MHz per effect so the
      cycle-timed beeper stays in tune, then restores 28 MHz).
- [x] **Phase 11** — Pathfinding & polish: monsters path with a per-turn BFS
      "Dijkstra map" from the hero (routing around walls, no more getting stuck);
      corridor line-of-sight so chasers are visible (rooms revealed on entry);
      render optimizations and flicker-free status/message lines.
- [x] **Phase 12** — Dimmed memory: terrain you've explored but can't currently
      see is drawn in a darker shade (a second, halved palette), distinct from
      what's in view.
- [x] **Phase 13** — Scrolls & rings: scrolls (`?`, read with `r`) with random
      effects (magic mapping / teleport) and a ring of protection (`=`, put on
      with `P`, improves AC). Bounded the BFS queue to fix a RAM overflow.
- [x] **Phase 14 — Win condition**: the Amulet of Yendor (`"`) waits on the
      deepest level (Dlvl 50), in place of its down-stairs; carry it back up and
      climb the stairs on Dlvl 1 to win the game.
- [x] **Phase 15 — Save/restore**: `S` saves the whole game (seed, player,
      inventory, per-depth persistence and explored map) to `nexthack.sav` on the
      SD card and returns to the title; it is loaded automatically on the next
      boot and then deleted (NetHack-style — no save-scumming).
- [x] **Phase 16 — Deeper dungeon**: the dungeon now descends to Dlvl 50. Cheap
      per-level state (gold/items/kills) is kept for every level; the fog-of-war
      lives in an 8-level **LRU pool** (recently visited levels stay mapped,
      distant ones are forgotten) so RAM stays flat with depth.
- [x] **Phase 17 — Item variety**: the inventory is now a per-item record
      (type + enchantment + erosion); a catalogue gives several weapons and
      armours (resolved by depth, sometimes enchanted), two potions and typed
      scrolls/rings. `w`/`W`/`P` equip the best you carry; stepping on an item
      names it ("You see here a +1 short sword"); the pack holds 24 in two columns.
- [x] **Phase 18 — Equipment corrosion**: an acid blob (`a`) corrodes your worn
      armour when it hits you and your wielded weapon when you strike it; eroded
      gear reads "rusty"/"corroded" and loses effectiveness (erosion capped at 3).
- [x] **Phase 19 — Wandering monsters**: a small per-turn chance to spawn a new
      monster (NetHack-style), so a level is never permanently cleared by camping.
- [x] **Phase 20 — Code banking (breaks the 64 KB ceiling)**: the engine is split
      into a resident half (hot code + all data in `0x8000-0xBFF0`) and cold code
      banked into the `0xC000` window, via z88dk `__banked`. Frees ~19 KB for new
      code. Needs the nightly z88dk (see Build).
- [x] **Phase 21 — Shops**: ~1/3 of levels (depth 2+) hold a shop room stocked
      with priced items and a stationary shopkeeper (`@`). Picking an item up in a
      shop buys it (gold deducted, refused if you can't pay); `d` sells a carried
      item back for half price. Gives the hero's gold a purpose at last.
- [x] **Phase 22 — Special levels (framework + Big Room)**: a deterministic
      dispatcher in `gen_level` turns certain depths into hand-built landmark
      levels, without perturbing the procedural RNG (and persistence) of the
      normal levels. The first kind — the **Big Room** (every 11th depth:
      11/22/33/44) — fills the whole playable area with one giant lit chamber,
      scattered with loot and a crowd of monsters.
- [ ] Later — more special levels: treasure vault (Phase 23) and hand-drawn
      map templates (Phase 24), both reusing this framework.
- [ ] Polish (minor, low priority) — show each held item's graphic tile beside its
      name on the inventory screen (`i`); tackle after the items above.

## Project structure

The code is split into modules with clear responsibilities (the platform
layer is kept separate from game logic):

| File | Responsibility |
|------|----------------|
| `platform.c` / `.h` | ZX Next hardware: tilemap, font, graphic tiles, palette, text/messages, keyboard |
| `rng.c` / `.h` | random number generator |
| `level.c` / `.h` | terrain buffer, procedural generation, level persistence |
| `monster.c` / `.h` | monsters: spawning, chase AI, combat, XP |
| `item.c` / `.h` | inventory and items (pick up, wield/wear/quaff/eat) |
| `sfx.c` / `.h` | beeper sound effects |
| `nexthack.c` | game state, main loop, map/status rendering, title screen |
| `game.h` | shared player/run state used across modules |

## Build

This repository contains **only the game source**. The toolchain and emulator are
external tools you install yourself; the build/run scripts expect them as sibling
folders of this one. The game is **code-banked** (>64 KB), which needs a recent
z88dk (the `__banked` trampoline, build v24836+):

```
<parent>/
├─ nexthack/        ← this repository
├─ z88dk-latest/    ← z88dk SDK, nightly v24836+  (https://github.com/z88dk/z88dk)
└─ CSpect/          ← the CSpect emulator         (https://mdf200.itch.io/cspect)
```

With that layout in place (`build.ps1` is the faster incremental+parallel build):

```bat
build.bat            REM builds the whole game (all modules) -> nexthack.nex
build.bat foo.c      REM builds a single .c file             -> foo.nex
```

Equivalent direct invocation of the full build:

```bat
set ZCCCFG=..\z88dk-latest\lib\config\
set PATH=..\z88dk-latest\bin;%PATH%
zcc +zxn -subtype=nex -vn -SO3 -clib=sdcc_iy --max-allocs-per-node200000 -startup=1 -pragma-include:zpragma.inc -m mainentry.c nexthack.c platform.c platform_init.c rng.c level.c levelgen.c levelfov.c monster.c monster_ai.c item.c sfx.c -o nexthack -create-app
```

The banking layout is configured by `zpragma.inc` (stack at `0xBFF0`, banking
segment 3) and `mmap.inc` (the `PAGE_20_CODE`/`PAGE_22_CODE` page ORGs).

## Run on CSpect

CSpect 3.x boots with a built-in Next ROM, so **no system ROM or SD card image is
required**.

```bat
run.bat              REM runs nexthack.nex on CSpect
```

> Note: `run.bat` passes `-mmc` a non-existent `.img` path on purpose. Pointing
> `-mmc` at a real folder makes CSpect mount it as the SD root, find no NextZXOS,
> and drop into 48K BASIC instead of autoloading the `.nex`.

### Testing save/restore

`S` saves to the SD card, so it needs a mounted, writable filesystem — which the
plain `run.bat` (no SD) does not provide. Use `run-sd.bat`, which copies
`nexthack.nex` into the NextZXOS SD image (`..\CSpect\zxnext.sd`) with `hdfmonkey`
and boots from it; then run `/nexthack.nex` from the NextZXOS Browser.

```bat
build.bat            REM build first
run-sd.bat           REM deploy into the SD image and boot NextZXOS
```

## Controls

| Key                       | Action          |
|---------------------------|-----------------|
| cursor keys / `h j k l`   | move ◄ ▼ ▲ ► (hold to keep moving) |
| `y` `u` `b` `n`           | move diagonally |
| `>` `<` or `Enter`        | stairs down / up |
| `s` or `.`                | wait / search |
| `,`                       | pick up the item under you |
| `i`                       | show inventory |
| `w` / `W`                 | wield weapon / wear armor |
| `P`                       | put on a ring |
| `q` / `e` / `r`           | quaff potion / eat food / read scroll |
| `S`                       | save game and quit to the title |

Walk into a monster to attack it; walk over gold to pick it up.

## Map & item tiles

The world is drawn as colourful 8×8 pixel-art tiles, not ASCII text. The tiles and
the entities they depict (the symbol in parentheses is the internal map code, kept
from the roguelike tradition):

- **Terrain:** floor (`.`), corridor (`#`), wall (`-` `|`), door (`+`),
  stairs up/down (`<` `>`)
- **Items:** gold (`$`), weapon (`)`), armor (`[`), potion (`!`), food (`%`),
  scroll (`?`), ring (`=`), the Amulet of Yendor (`"`)
- **Creatures:** hero (`@`), rat (`r`), bat (`B`), acid blob (`a`), kobold (`k`),
  dog (`d`), snake (`S`), orc (`o`), zombie (`Z`)

## Technical notes

- **Display**: the Next hardware tilemap (80×32). Glyphs are built at runtime by
  expanding the 1bpp ROM font at `0x3C00` into 4bpp tiles; per-cell colour comes
  from the tilemap palette (16 ink colours over a black paper). The ULA layer is
  disabled so only the tilemap is shown. Tile data lives in Bank 5
  (`0x4000` tiles, `0x6000` tilemap) — free because the program is at `0x8000+`.
- **`int` is 16-bit** in SDCC, so values that can exceed ±32767 (gold, the turn
  counter, bit flags) need care: `long` works but is slow, and 16-bit arithmetic
  must be audited for overflow.
- **Memory / code banking**: the engine outgrew the 64 KB the Z80 sees at once, so
  it is **code-banked** — a resident half (hot code + all data + stack in
  `0x8000-0xBFF0`) plus cold code in two 16 KB pages swapped into the `0xC000` window
  by z88dk's `__banked` trampoline. The BFS scratch arrays live in Bank 5's free tail.
  This freed ~19 KB for new code; the resident *data* budget stays tight.

## References

- **ZX Spectrum Next dev wiki (primary reference):** <https://wiki.specnext.dev/Main_Page>
  - Memory map (default config has 512 KB RAM): <https://wiki.specnext.dev/Memory_map>
- ZX Spectrum Next — Tilemap mode: <https://www.specnext.com/tilemap-mode/>
- ZX Spectrum Next — Sprites: <https://www.specnext.com/sprites/>
- NetHack: <https://www.nethack.org/>
- z88dk: <https://github.com/z88dk/z88dk>
- CSpect emulator: <https://mdf200.itch.io/cspect>

## License

Copyright © 2026 Leonardo Roman da Rosa

NextHack is free software, released under the **GNU General Public License,
version 3 or (at your option) any later version** (SPDX identifier
`GPL-3.0-or-later`). The full text of GPLv3 is in [LICENSE](LICENSE); for later
versions see <https://www.gnu.org/licenses/>.

It is an independent, from-scratch engine *inspired by* NetHack's design. It
contains no NetHack source code and is **not affiliated with or endorsed by** the
NetHack DevTeam; "NetHack" is mentioned only to credit the inspiration. Because the
codebase shares no code with NetHack, NetHack's own licence (the NGPL) does not
apply to it, leaving us free to license NextHack under the GPLv3.
```
