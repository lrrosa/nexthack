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
- [ ] **Phase 3** — Monsters and combat (`mon.c`, `uhitm.c`, `mhitu.c`).
- [ ] **Phase 4** — Inventory and items (`invent.c`, `objnam.c`, `pickup.c`).
- [ ] **Phase 5** — Save/restore via the NextZXOS/esxDOS file API.

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
| `W` `A` `S` `D` / arrows  | move ▲ ◄ ▼ ►    |
| `Q` `E` `Z` `C`           | move diagonally |
| `H` `J` `K` `L` / `Y` `U` `B` `N` | vi-keys (also work) |
| `>` / `<`                 | descend / ascend stairs |
| `.` or space              | wait (passes the turn) |

## Map legend

`@` hero · `.` floor · `#` corridor · `-` `|` wall · `+` door ·
`>` down stairs · `$` gold · `%` food · `d` dog · `r` rat

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
