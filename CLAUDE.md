# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

NextHack is a NetHack-inspired roguelike written in C for the **ZX Spectrum
Next** (Z80N), built with **z88dk** (the `zsdcc`/SDCC compiler) and tested on the
**CSpect** emulator. It is a fresh engine on NetHack's design, not a recompile of
NetHack's source — sized to fit the Z80.

**Project status & roadmap:** see the phase checklist in `README.md` (done items
and the "Later" list) and `git log --oneline` — each commit is one phase. Start a
session from this folder so this file is auto-loaded.

## Build & run

The z88dk SDK and CSpect live one directory up (`..\z88dk-latest`, `..\CSpect`);
they are not in this repo. **The build needs the nightly z88dk** (`..\z88dk-latest`,
v24836+) for its `__banked` trampoline — the game is **code-banked** (see Memory
budget). The old pinned `..\z88dk` (v23854) lacks it and can no longer build this
source. Build/run are scripts run from this folder:

```bat
build.bat            REM builds the whole game -> nexthack.nex (+ nexthack.map)
build.bat foo.c      REM builds a single .c file -> foo.nex
run.bat              REM runs nexthack.nex in CSpect (no SD; can't test save)
run.bat foo.nex      REM runs a specific .nex
run-sd.bat           REM deploys into zxnext.sd and boots NextZXOS (tests save)
```
```powershell
.\build.ps1          # incremental + parallel build -> nexthack.nex (preferred)
.\build.ps1 -Clean   # force a full rebuild
```

`build.bat` sets `ZCCCFG`/`PATH` (to `..\z88dk-latest`) and invokes one zcc pass:

```
zcc +zxn -subtype=nex -vn -SO3 -clib=sdcc_iy --max-allocs-per-node200000 -startup=1 -pragma-include:zpragma.inc -m <srcs> -o nexthack -create-app
```

`zpragma.inc` (`REGISTER_SP=0xBFF0`, `CRT_APPEND_MMAP=1`, `CLIB_BANKING_SEGMENT=3`)
and `mmap.inc` (the `PAGE_20_CODE`/`PAGE_22_CODE` section ORGs) drive the banking.
That single pass recompiles **everything** (~3 min). `build.ps1` does the same
compile per `.c` to a `.o` (byte-identical output — verified same SHA-256), but
**skips untouched modules and parallelises** across cores: clean ~75s, one-module
edit ~25s, no-op ~2s. Prefer it; `build.bat` is the single-shot fallback.

When adding a new `.c` module, add it to the `SRCS`/`$srcs` list in **both**
`build.bat` and `build.ps1`, and decide resident vs banked (see Memory budget).

There are no automated tests. Verification is manual: build, then run in CSpect and
observe. The build agent cannot see the emulator window, so behaviour is confirmed
by the human running `run.bat`.

### Gotchas that waste time
- **`run.bat`'s `-mmc` must point at a non-existent `.img`** (it does). Point it at a
  real folder and CSpect mounts it as an SD root, finds no NextZXOS, and drops to
  48K BASIC instead of autoloading the `.nex`. "Booted to BASIC" almost always means
  this, or an invalid `.nex`, or a startup crash (see memory budget below).
- CSpect 3.x has a **built-in Next ROM**, so no system ROM/SD image is needed.
- SDCC's `warning 110 ... "EVELYN the modified DOG"` is a **harmless** peephole-optimizer
  message; ignore it.
- SDCC `int` is **16-bit**. Watch for overflow; `long` works but is slow.
- In PowerShell, `Set-Location` does **not** change .NET's cwd: `[IO.File]::ReadAllBytes`
  needs an absolute path.

## Architecture

Strict split between the **Next hardware platform layer** and **game logic**:

Modules are also split by **resident vs banked** (see Memory budget). Header `.h`
files declare the interface; the `.c` is resident (R) or banked (B):

| File | R/B | Responsibility |
|------|-----|----------------|
| `mainentry.c` | R | `main()` only: the turn loop / dispatcher (CRT entry — can't be banked) |
| `platform.c/.h` | R | hot Next hardware: tilemap draw primitives, text/messages, keyboard, file I/O; `master`/`inkcol` palette tables |
| `platform_init.c` | B | one-time setup: tilemap/font/tile/palette init (`tm_init`) + the `gfx[]` tile table (const-banked) |
| `rng.c/.h` | R | xorshift16 PRNG + `world_seed` |
| `level.c/.h` | R | terrain buffer + the per-cell leaves `terrain`/`walkable`/`tile_for`; `.h` declares the whole level interface |
| `levelgen.c` | B | procedural generation + gold/item persistence (owns room table + masks) |
| `levelfov.c` | B | field of view + save/restore (owns the fog-of-war pool) |
| `monster.c/.h` | R | monster arrays + per-monster leaves (`monster_at`, `mon_find`, `mon_tile`, `pick_mon`); catalogue |
| `monster_ai.c` | B | BFS chase, combat, spawning, kill-persistence |
| `item.c/.h` | B | inventory and item actions (pick up, wield/wear/quaff/eat/read/put-on) |
| `sfx.c/.h` | B | beeper sound effects |
| `nexthack.c/.h` | B | game-state globals (resident DATA) + rendering, turn step, level orchestration, save/restore, screens; `.h` declares its `__banked` entry points for `mainentry.c` |
| `game.h` | — | shared player/run state (`extern`s defined in `nexthack.c`) |

Shared mutable state (`hero_x/y`, `dlvl`, `php`, `gold`, `ac`, `xp`, `nutrition`, …)
is declared `extern` in `game.h` and **defined once in `nexthack.c`** (as resident
DATA — banked code's data is resident too). Modules include `game.h` to read/write it.

### Display (the tilemap)
- Renders on the Next **hardware tilemap, 80×32 chars** (`TM_W`×`TM_H`). The ULA layer
  is disabled (NextReg 0x68); only the tilemap shows. Runs at 28 MHz (NextReg 0x07).
- Tile/map data lives in **Bank 5** (fixed at CPU `0x4000-0x7FFF`, free because the
  program is at `0x8000+`): tile definitions at `0x4000`, tilemap at `0x6000`
  (NextReg 0x6F/0x6E). **Tile definitions must stay below `0x5C00`** (NextZXOS sysvars).
- **Tiles 0..127 = ROM font** (expanded from `0x3C00`), used for text and coloured
  per cell via the attribute's palette **offset** (see palette below). Tiles 128+
  (`T_*` in `platform.h`) are **4bpp colour graphic tiles** (`gfx[]` in `platform_init.c`,
  const-banked) for map cells. Adding a graphic tile: add a `T_*` number, an entry in
  `gfx[]`, and bump the count in `load_gfx_tiles()`.
- **Palette offsets** (tilemap palette, NextReg 0x43=0x30):
  - offset 0 (indices 0..15) = full-colour **master palette** for graphic tiles in view.
  - offset 1 (indices 16..31) = **dimmed master** (channels halved) for remembered,
    out-of-sight terrain. Half is the dimmest a grey stays neutral on 3-3-2 colour.
  - offsets 2..15 = black-paper/ink pairs for **coloured text** (`inkcol[]`).
  - The ROM `$` glyph is blank in CSpect's ROM, so a graphic `T_DOLLAR` tile is used
    for gold in the status bar.
- **Rendering is a single full redraw per turn** (`draw_map` in `nexthack.c`) using a
  running tilemap pointer and inline FOV bit-tests for speed; writing each cell exactly
  once (rather than clear-then-fill) is what keeps it flicker-free. Status/message
  lines follow the same write-once-then-pad rule.

### Level generation, persistence & FOV (`level.c`)
- The map is a `char lvl[MAPH][MAPW]` grid (`'.'` floor, `#` corridor, `-`/`|` wall,
  `+` door, `<`/`>` stairs, `$`/`)`/`[`/`!`/`%`/`?`/`=`/`"` items, `' '` rock). Monsters are
  **not** in this buffer (they live in `monster.c` arrays and are drawn on top).
- **Persistence is deterministic**, not stored maps: `gen_level()` calls
  `rng_set(level_seed(dlvl))` so a given depth always regenerates identically. Player
  changes (gold taken, monsters killed, items picked up) are kept in tiny per-depth
  **bitmasks** and re-applied after regeneration. **Do not change the order/number of
  `rn2()` calls inside generation casually** — it changes every level and can desync
  the persistence bit indices.
- `build_level()` (in `nexthack.c`) orchestrates: `gen_level()` → spawn monsters →
  apply gold/monster/item persistence. Gold/items are placed before monsters so
  spawning sees the same map each visit.
- **Win condition**: the deepest level (`DLVL_AMULET`, currently 50) has no
  down-stairs — the Amulet of Yendor (`"`) sits on that cell instead. Picking it up
  sets `has_amulet`; climbing `<` on Dlvl 1 while carrying it sets `won` (victory
  screen, then restart). The amulet is placed **without RNG** (on the would-be
  down-stairs cell), so it cannot desync the deterministic per-depth generation.
- **FOV** remembers explored cells in an **LRU pool** (`fov_pool`: the `FOV_SLOTS`
  most recently visited levels' 1-bit-per-cell maps; entering a new level evicts and
  forgets the least-recently-used one) plus a recomputed-each-turn `vis_now` bitmap.
  Visibility = the hero's room (lit on entry)
  + radius 1 + line-of-sight rays down corridors (walls/rock/**doors are opaque**, so
  rooms are only revealed on entry). `draw_map` shows unseen=black, in-sight=full,
  seen-but-not-visible=dimmed.

### Save / restore (`nexthack.c` + per-module `*_save`/`*_load`)
- **Model**: NetHack-style *save & quit*. `S` writes the whole game to
  `nexthack.sav` (a magic+version header, the player struct, then each module's
  state) and returns to the title; the boot path calls `load_game()`, which
  restores and then **deletes** the file (no save-scumming), else starts fresh.
- The *current* level is **not** saved: `build_level()` regenerates it
  deterministically from the restored `world_seed` + persistence bitmasks, exactly
  as a revisit does. Saved state = `world_seed`, the player globals, the inventory
  (`item.c`), and the per-depth `gold_taken`/`item_taken`/`mon_dead` masks plus the
  `fov_pool` LRU fog-of-war (`level.c`/`monster.c`).
- File I/O lives in the **platform layer** (`file_*` in `platform.c`, wrapping
  esxDOS `esx_f_*`). It needs a mounted writable filesystem, which `run.bat` does
  **not** give — use **`run-sd.bat`** (copies the `.nex` into `..\CSpect\zxnext.sd`
  with `hdfmonkey`, boots NextZXOS; run the `.nex` from the Browser).
- Memory budget: the cheap per-level masks scale to all 50 levels (`MAXLVL =
  DLVL_AMULET`, ~3 bytes/level), but the fog-of-war does **not** — it is a fixed
  `FOV_SLOTS`-entry LRU pool, so RAM is independent of dungeon depth. esxDOS itself
  adds ~1.5 KB of BSS (sector buffers), so `FOV_SLOTS` (8) is kept below the RAM
  max (~18) to reserve headroom for future features.

### Items (`item.c`)
- The inventory is an `obj_t[]` (an `otyp` into the `objtypes[]` catalogue, an
  `ench` enchantment, an `ero` erosion level and a `worn` flag) — up to 24,
  drawn in two columns on the inventory screen.
- The floor only stores an item's **class char** (`)` `[` `!` `%` `?` `=`), so
  generation/persistence stays untouched. The *specific* object (which weapon,
  what enchantment) is resolved when the cell is looked at or picked up,
  deterministically from `(dlvl, x, y, world_seed)` via a side hash that does
  **not** touch the RNG stream — so a floor item is always the same thing and
  stays in sync with the deterministic generation. Better/enchanted items appear
  deeper (`mindep` per catalogue entry).
- `w`/`W`/`P` equip the **best** carried weapon/armour/ring (highest
  `prop + ench - ero`). The combat globals (`weapon_dmg`, `armor_def`, `ac`) are
  recomputed by `recompute_gear()` from the worn items.
- `q`/`e`/`r` use `select_item()`: silent when you carry one type, but it pops a
  letter menu when two *different* types are present. These three set
  `acted`/`turns` themselves, so a cancel or a no-op costs no turn.

### Monster AI (`monster.c`)
- Monster types are a table (`montypes[]`: char, hp, damage, xp, min depth, tile,
  `corr`, name); `spawn_level_monsters()` draws from the depth-appropriate pool.
  HP/damage scale with depth. A `corr` (corrosive) monster — the acid blob — calls
  `corrode_worn()` (in `item.c`) to rust the hero's armour when it bites and the
  wielded weapon when struck; erosion raises `obj_t.ero`, capped at 3.
- Pathfinding is a **per-turn BFS "Dijkstra map"** from the hero (`compute_dist_map`)
  over walkable cells; each monster steps to the lowest-distance neighbour. One search
  serves all monsters. The BFS frontier queue `bfsq` is **bounded** (`BFSQ_SIZE`) with
  guarded enqueues — do not size it to `MAPW*MAPH` (see memory budget).

### Turn loop & input (`nexthack.c main`)
- Loop: read key → act → if the action took a turn, `upkeep()` (hunger/regen) then
  `monsters_turn()` → recompute FOV → redraw → handle death → `in_pause(40)` to
  throttle held-key movement.
- Movement: cursor keys **and** vi-keys (`hjkl`+`yubn`). Commands are NetHack-style
  single letters (`,` `i` `w` `W` `P` `q` `e` `r` `S`, `>`/`<`/Enter for stairs). Uppercase
  is **not** folded to lowercase (so `w` wield vs `W` wear are distinct).

### Sound (`sfx.c`)
- Beeper effects via z88dk's `bit_beepfx`. The effects are cycle-timed for 3.5 MHz, so
  `sfx_*` drops the CPU to 3.5 MHz for the effect and restores 28 MHz after.

## Memory budget — the constraint to respect (CODE-BANKED architecture)
The game **broke the 64 KB ceiling by code-banking**. Layout:
- **Resident** (`0x8000-0xBFF0`, one 16 KB bank): hot code + **all** data/BSS + the
  512 B stack (`REGISTER_SP=0xBFF0`). This is the tight half.
- **Banked** (`0xC000-0xFFFF`, `CLIB_BANKING_SEGMENT=3`): cold code in two pages,
  `PAGE_20_CODE` (Bank 10) and `PAGE_22_CODE` (Bank 11), mapped in on demand by the
  z88dk `__banked` trampoline. **~19 KB free here for new code.**
- **Bank 5** (`0x4000-0x7FFF`, always mapped): tilemap + tile defs, and its free tail
  (`0x7400-0x8000`) holds the BFS scratch `dist[]`+`bfsq[]` (data-banked out of resident).

**The resident half is the constraint.** Everything resident (code+data+BSS) must end
below the stack floor `~0xBDF0`, or the stack corrupts and the machine resets to BASIC.
Check `__CODE_END_tail` / `__BSS_END_tail` in `nexthack.map` after any change. Current:
`__CODE_END=$AAA2`, `__BSS_END=$BBB1` — about **~575 B** to the stack floor (after
const-banking `gfx[]`, see below).

**Adding a feature:**
- **New code → make it banked** (it has room): put it in a module compiled into
  `PAGE_20_CODE`/`PAGE_22_CODE`, mark entry points `__banked`. Cold/per-turn code banks
  freely (the trampoline cost is negligible off the per-cell path).
- **New resident DATA is still the scarce resource.** Banked code's `static` data —
  **and its string/const literals (resident rodata)** — stay resident, so data/text-heavy
  features eat the ~575 B fast (the shops' message strings did). Levers when it overflows:
  (a) **const-bank read-once tables** — `gfx[]` (1600 B, read only by `load_gfx_tiles` at
  startup) lives in `platform_init.c` under `#pragma constseg PAGE_22_CODE`, so it sits in
  Bank 11 next to its reader (which runs with that page mapped); (b) data-bank scratch
  arrays into Bank 5's free tail (like `dist`/`bfsq`); (c) trim strings.
- **`--max-allocs-per-node200000` is load-bearing for resident code size** (the SDCC
  allocator's thoroughness shrinks code); don't lower it.

**Partitioning rules (how the split is done — see `nextzxos-banking-findings` memory):**
1. Split a mixed module into wholly-hot/wholly-cold **files** — in-file `#pragma codeseg`
   does NOT partition by position (it scrambles sections).
2. A module containing `main()` → bank the module, move only `main()` to a tiny resident
   file (`mainentry.c`); the CRT jumps straight to main, so it can't be banked.
3. Keep the per-cell/per-move **leaves** resident (`platform.c` draw primitives,
   `level.c` terrain/walkable/tile_for, `monster.c` monster_at/mon_find/…) so banked
   callers reach them by direct calls; banked entry points are `__banked`, intra-page
   static helpers stay plain. (Read-once tables like `gfx[]` are *not* leaves — bank them.)
4. Data shared across a split is defined in one file, `extern` in the other (DATA is
   resident regardless of which file's code is banked).
5. Resident modules: `mainentry.c`, `platform.c`, `level.c`, `monster.c`, `rng.c`.
   Banked: `nexthack.c`+`platform_init.c` (PAGE_22); `item.c`,`levelgen.c`,`levelfov.c`,
   `monster_ai.c`,`sfx.c` (PAGE_20).
