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

The z88dk SDK and CSpect live one directory up (`..\z88dk`, `..\CSpect`); they are
not in this repo. Build/run are Windows batch scripts (run from this folder):

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

`build.bat` sets `ZCCCFG`/`PATH` and invokes (one zcc pass over all modules):

```
zcc +zxn -subtype=nex -vn -SO3 -clib=sdcc_iy --max-allocs-per-node200000 -m <srcs> -o nexthack -create-app
```

That single pass recompiles **everything** every time (~4 min). `build.ps1` does
the same compile per `.c` (byte-identical output — zcc/SDCC compile per
translation unit, so `--max-allocs` quality is unchanged), but **skips untouched
modules and parallelises** across cores: clean ~80s, one-module edit ~25s, no-op
~2s. Prefer it for iteration; `build.bat` stays as the simple single-shot fallback.

When adding a new `.c` module, add it to the `SRCS`/`$srcs` list in **both**
`build.bat` and `build.ps1`.

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

| File | Responsibility |
|------|----------------|
| `platform.c/.h` | All ZX Next hardware: tilemap setup, tile/font/palette, drawing primitives, text/messages, keyboard |
| `rng.c/.h` | xorshift16 PRNG + `world_seed` |
| `level.c/.h` | terrain buffer, procedural generation, persistence, field of view |
| `monster.c/.h` | monster catalogue, BFS pathfinding, combat, XP |
| `item.c/.h` | inventory and item actions (pick up, wield/wear/quaff/eat/read/put-on) |
| `sfx.c/.h` | beeper sound effects |
| `game.h` | shared player/run state (`extern`s defined in `nexthack.c`) |
| `nexthack.c` | game state definitions, main loop, map/status rendering, title screen |

Shared mutable state (`hero_x/y`, `dlvl`, `php`, `gold`, `ac`, `xp`, `nutrition`, …)
is declared `extern` in `game.h` and **defined once in `nexthack.c`**. Modules include
`game.h` to read/write it.

### Display (the tilemap)
- Renders on the Next **hardware tilemap, 80×32 chars** (`TM_W`×`TM_H`). The ULA layer
  is disabled (NextReg 0x68); only the tilemap shows. Runs at 28 MHz (NextReg 0x07).
- Tile/map data lives in **Bank 5** (fixed at CPU `0x4000-0x7FFF`, free because the
  program is at `0x8000+`): tile definitions at `0x4000`, tilemap at `0x6000`
  (NextReg 0x6F/0x6E). **Tile definitions must stay below `0x5C00`** (NextZXOS sysvars).
- **Tiles 0..127 = ROM font** (expanded from `0x3C00`), used for text and coloured
  per cell via the attribute's palette **offset** (see palette below). Tiles 128+
  (`T_*` in `platform.h`) are **4bpp colour graphic tiles** (`gfx[]` in `platform.c`)
  for map cells. Adding a graphic tile: add a `T_*` number, an entry in `gfx[]`, and
  bump the count in `load_gfx_tiles()`.
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

## Memory budget — the constraint to respect
The program (code + data + BSS + stack) lives in `0x8000-0xFFFF` (32 KB; Bank 5 below
is the tilemap). The stack pointer is `0xFF58` with a 512-byte stack, so **BSS must end
well below `~0xFD58`** or it corrupts the stack and the machine resets to BASIC. The
`.nex` is two 16K banks. Large `static` arrays are the danger — check
`__BSS_END_tail` vs `__register_sp` in `nexthack.map` after adding any. This already bit
the BFS queue once (it was `MAPW*MAPH`).

**`--max-allocs-per-node200000` is also load-bearing for code *size*, not just
quality/speed.** The SDCC register allocator's thoroughness shrinks the code; measured
`__BSS_END_tail` by setting: 200000 → `$FCD6` (+130 B headroom), 25000 → `$FD08` (+80 B),
default 3000 → `$FEE1` (**−393 B, overflows the stack → boots to BASIC**). So do **not**
lower it to speed up compiles — the budget can't spare the ~500 B the code grows. For
faster builds use `build.ps1` (incremental/parallel, same 200000, byte-identical) instead.
Current headroom is only ~130 B, so a new feature that overflows may need code shrinking,
not a `max-allocs` bump.
