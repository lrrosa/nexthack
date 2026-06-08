# NetHack Next — porting NetHack 3.5.0 to the ZX Spectrum Next

A port of NetHack to the [ZX Spectrum Next](https://www.specnext.com/), built
with **Z88DK** (the `zsdcc`/SDCC C compiler) and tested on the **CSpect** emulator.

## Strategy

Chosen target: **pragmatic hybrid** — reuse as much of the original NetHack 3.5.0
C engine as fits on the Z80N, with a reduced scope (smaller maps, fewer
monsters/items, no Lua/special levels at first). You cannot "compile all 130
files and fix the errors" — it is ~250k lines of C and the Z80 only sees 64 KB at
a time. The path is to build a **Next platform layer** (display, keyboard, files)
and pull in the real NetHack modules bottom-up.

## Status

- [x] **Phase 0 — Toolchain** validated: `zcc` compiles C → a valid `.nex`.
- [x] **Phase 1 — Vertical slice**: a NetHack-style level (2 rooms + corridor),
      hero `@` moving with NetHack's vi-keys, wall collision, decorative
      monsters/items/stairs.
- [x] **Phase 1b — 80-column display** ([nhnext.c](nhnext.c)): rendered on the
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
- [ ] **Phase 14** — Save/restore via the NextZXOS/esxDOS file API.
- [ ] Later — equipment erosion; shops; special levels; a win condition.

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
| `nhnext.c` | game state, main loop, map/status rendering, title screen |
| `game.h` | shared player/run state used across modules |

## Build

Prerequisite: the `..\z88dk` folder (already in the project).

```bat
build.bat            REM builds hello.c
build.bat nhnext.c   REM builds nhnext.c -> nhnext.nex (+ nhnext.map)
```

Equivalent direct invocation:

```bat
set ZCCCFG=..\z88dk\lib\config\
set PATH=..\z88dk\bin;%PATH%
zcc +zxn -subtype=nex -vn -SO3 -clib=sdcc_iy --max-allocs-per-node200000 -m nhnext.c -o nhnext -create-app
```

## Run on CSpect

CSpect 3.x boots with a built-in Next ROM, so **no system ROM or SD card image is
required**.

```bat
run.bat              REM runs nhnext.nex on CSpect
```

> Note: `run.bat` passes `-mmc` a non-existent `.img` path on purpose. Pointing
> `-mmc` at a real folder makes CSpect mount it as the SD root, find no NextZXOS,
> and drop into 48K BASIC instead of autoloading the `.nex`.

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

Walk into a monster to attack it; walk over gold to pick it up.

## Map & item legend

`@` hero · `.` floor · `#` corridor · `-` `|` wall · `+` door · `<` `>` stairs ·
`$` gold · `)` weapon · `[` armor · `!` potion · `%` food · `?` scroll · `=` ring

Monsters: `r` rat · `B` bat · `k` kobold · `d` dog · `S` snake · `o` orc · `Z` zombie

## Technical notes

- **Display**: the Next hardware tilemap (80×32). Glyphs are built at runtime by
  expanding the 1bpp ROM font at `0x3C00` into 4bpp tiles; per-cell colour comes
  from the tilemap palette (16 ink colours over a black paper). The ULA layer is
  disabled so only the tilemap is shown. Tile data lives in Bank 5
  (`0x4000` tiles, `0x6000` tilemap) — free because the program is at `0x8000+`.
- **`int` is 16-bit** in SDCC; NetHack uses `long` for many things (gold, time,
  flags) — it works but is slow and needs overflow auditing when porting.
- **Memory**: the Next has 1–2 MB in 8 KB banks; large code and data will need
  manual banking in later phases.

## References

- **ZX Spectrum Next dev wiki (primary reference):** <https://wiki.specnext.dev/Main_Page>
  - Memory map (default config has 512 KB RAM): <https://wiki.specnext.dev/Memory_map>
- ZX Spectrum Next — Tilemap mode: <https://www.specnext.com/tilemap-mode/>
- ZX Spectrum Next — Sprites: <https://www.specnext.com/sprites/>
- NetHack: <https://www.nethack.org/>
- z88dk: <https://github.com/z88dk/z88dk>
- CSpect emulator: <https://mdf200.itch.io/cspect>
```
