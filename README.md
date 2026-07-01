# NextHack — a NetHack-inspired roguelike for the ZX Spectrum Next & 128K

A from-scratch NetHack-style roguelike built with **Z88DK** (the `zsdcc`/SDCC C
compiler). **One codebase builds two targets:**

- the **ZX Spectrum Next** ([+zxn](https://www.specnext.com/)) — hardware tilemap,
  full-colour 8×8 tiles, Layer 2 title/victory art; tested on **CSpect**;
- the plain **ZX Spectrum 128K** (+zx) — ULA display, 1-bit UDG tiles, an
  edge-scrolling 32-column viewport over the 80-wide map, and attribute-clash SCR
  loading screens; tested on **ZEsarUX**.

The two share **all** game logic; only the platform/render layer differs, selected
at compile time by `#ifdef __ZXNEXT`. Both are **code-banked** to break the Z80's
64 KB ceiling (the Next via its MMU, the 128K via port `0x7FFD`).

**Downloads & history:** grab the latest `.nex` / `.tap` from the
[Releases](https://github.com/lrrosa/nexthack/releases) page; see
[CHANGELOG.md](CHANGELOG.md) for what changed in each version.

![NextHack title screen — a pixel-art loading screen: an armoured knight facing a demon in a torch-lit dungeon archway, beneath the NextHack logo.](docs/title.png)

*The title screen, on the Next's Layer 2 framebuffer (256×192); the 128K build
shows the same art as an attribute-clash SCR.*

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

## Features

- A **50-level dungeon**, generated procedurally and **deterministically**: each
  depth regenerates identically from its own seed, while the changes you make
  (gold taken, monsters killed, items picked up) are remembered across revisits.
- **Field of view** with fog of war — rooms light up on entry, corridors reveal
  around you, and explored-but-unseen terrain is drawn dimmed from memory.
- **Turn-based combat** against a depth-scaled bestiary (rats, bats, kobolds,
  dogs, snakes, orcs, zombies, acid blobs, leprechauns, yellow lights, homunculi,
  wraiths…) that chases you with BFS pathfinding. Many bite with a **special
  attack** — poison, blindness, sleep, gold theft, life-drain — and hidden
  **traps** (trap doors, darts, sleeping gas) lurk in the deeper floors. Scratch
  **Elbereth** (`E`) in the dust to keep monsters at bay; experience levels raise
  your HP.
- A **loyal pet dog** starts at your side, fights monsters for you and follows you
  through the dungeon. **Throw** (`t`) a weapon down a corridor for a ranged
  attack — it lands on the floor to be reclaimed — **search** (`s`) the ground for
  hidden traps, and **pray** (`p`) to your god to haul you out of trouble (best at
  an altar).
- **Items and equipment** — weapons, armour, potions, food, scrolls and rings,
  each with its own enchantment, erosion and **blessed/uncursed/cursed** state;
  potions and scrolls start **unidentified**. Wield/wear the best you carry,
  quaff/eat/read, watch acid blobs corrode your gear, beware cursed items that
  won't come off — and step onto an **altar** (`_`) to reveal the blessings on
  what you carry.
- **Shops** with priced goods and a shopkeeper to buy from and sell to, plus
  **special levels** — the cavernous Big Room, guarded **treasure vaults** (gold
  and superior gear behind tough monsters), and **hand-drawn maps** like a
  pillared temple, dropped in among the procedural floors.
- Hunger and slow HP regeneration, beeper sound effects, and **save & quit** to
  the SD card, NetHack-style (reloaded once on the next boot, then deleted — no
  save-scumming).
- The goal: retrieve the **Amulet of Yendor** from the bottom of the dungeon and
  climb back out alive.

![NextHack gameplay: a procedurally generated dungeon level drawn in colour 8×8 tiles, with the status and command bars below the map.](docs/gameplay.png)

*Exploring a dungeon level — rooms light up on entry, corridors reveal as you go, and gold, items and monsters share the floor.*

## Project structure

The code is split into modules with clear responsibilities — the platform
(hardware) layer kept separate from the game logic. Because the engine
is **code-banked**, each module is also either **resident** (hot code
that stays mapped in, R) or **banked** (cold code paged into the `0xC000` window
on demand, B). Headers declare the interface; the `.c` is the R/B half. The
source files live in **`src/`**; the build scripts, `mmap.inc` and `zpragma.inc`
stay at the repo root (the build runs from there, where z88dk looks for `mmap.inc`).

The platform/render layer is **dual-target** via `#ifdef __ZXNEXT`: `platform.c`,
`platform_init.c` and `nexthack.c`'s renderer carry both the Next (tilemap /
Layer 2) and the 128K (ULA / UDG / SCR) code paths in one file, and the build
picks the right one. The 128K target adds `scr.c` (SCR blitter), `title_scr.c` /
`victory_scr.c` (const-banked SCRs) and `banked_call.asm` (the `0x7FFD`
trampoline); the Next target adds the `titlegfx*` / `victorygfx*` Layer 2 images.
The table below describes the **Next** build; the 128K build's modules are the
same minus the Layer 2 images.

| File | R/B | Responsibility |
|------|-----|----------------|
| `mainentry.c` | R | `main()` only: the turn loop / dispatcher (the CRT entry point) |
| `platform.c` / `.h` | R | hot ZX Next hardware: tilemap draw primitives, text/messages, keyboard, file I/O; palette tables |
| `platform_init.c` | B | one-time setup: tilemap/font/tile/palette init and the `gfx[]` graphic-tile table |
| `rng.c` / `.h` | R | random number generator (xorshift16) and the world seed |
| `level.c` / `.h` | R | terrain buffer and the per-cell leaves (terrain / walkable / tile lookup) |
| `levelgen.c` | B | procedural generation, special levels, gold/item persistence |
| `levelfov.c` | B | field of view (fog of war) and save/restore |
| `monster.c` / `.h` | R | monster arrays, per-monster lookups and the bestiary |
| `monster_ai.c` | B | chase pathing (BFS), combat, spawning, kill persistence, the pet |
| `item.c` / `.h` | B | inventory and items (pick up, wield/wear/quaff/eat/read/put-on, throw) |
| `sfx.c` / `.h` | B | beeper sound effects |
| `nexthack.c` / `.h` | B | game-state globals (resident data), rendering, turn step, level orchestration, save/restore, screens |
| `game.h` | — | shared player/run state used across modules |

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

With that layout in place, `build.ps1` is the preferred build — incremental and
parallel, so it skips untouched modules and uses all cores (clean ~75 s, a
one-module edit ~25 s):

```powershell
.\build.ps1          # incremental + parallel build -> nexthack.nex
.\build.ps1 -Clean   # force a full rebuild
```

`build.bat` is the single-shot fallback (recompiles everything in one pass):

```bat
build.bat            REM builds the whole game (all modules) -> nexthack.nex
build.bat foo.c      REM builds a single .c file             -> foo.nex
```

Equivalent direct invocation of the full build:

```bat
set ZCCCFG=..\z88dk-latest\lib\config\
set PATH=..\z88dk-latest\bin;%PATH%
zcc +zxn -subtype=nex -vn -SO3 -clib=sdcc_iy --max-allocs-per-node200000 -startup=1 -pragma-include:zpragma.inc -m src/mainentry.c src/nexthack.c src/platform.c src/platform_init.c src/rng.c src/level.c src/levelgen.c src/levelfov.c src/monster.c src/monster_ai.c src/item.c src/sfx.c src/titlegfx0.c src/titlegfx1.c src/titlegfx2.c src/titlepal.c -o nexthack -create-app
```

The banking layout is configured by `zpragma.inc` (stack at `0xBFF0`, banking
segment 3) and `mmap.inc` (the `PAGE_20_CODE`/`PAGE_22_CODE` page ORGs).

### Building the ZX Spectrum 128K target

The plain 128K build uses the classic `+zx` target with manual 16 KB bank paging
(port `0x7FFD`). It needs `..\ZEsarUX\` as a sibling folder to run.

```powershell
.\build-zx128.ps1          # incremental -> nexthack128.tap (+ a 128K nexthack128.sna)
.\build-zx128.ps1 -Clean   # force a full rebuild
```

It compiles the same modules (taking their `#else` 128K code paths) plus the ULA
SCR screens (`scr.c`, `title_scr.c`, `victory_scr.c`) and the vendored `0x7FFD`
banking trampoline (`banked_call.asm`); the 96 KB of Next Layer 2 image modules
are dropped. Banking comes from `zpragma-zx128.inc` (the `CRT_ORG_BANK_N`
far-bank ORGs); `tools/png2scr.py` regenerates the title/victory SCRs from the
PNG art.

## Run the Next build

Both targets run in the **ZEsarUX** emulator (kept one dir up, in `..\ZEsarUX`).

```bat
run-next.bat         REM runs nexthack.nex in ZEsarUX (Next)
```

ZEsarUX auto-mounts esxDOS onto the folder holding the `.nex`, so **save/restore
works directly**: in game, `S` writes `nexthack.sav` beside the `.nex` and the
next boot reloads it — no SD card image or `hdfmonkey` needed.

## Run the ZX Spectrum 128K build

**`nexthack128.tap` is the distributable.** It is a standard tape that loads on
real 128K hardware and any accurate emulator (Spectaculator, Fuse, …): open it on
a 128K model and let it load. Its BASIC loader pages each 16 KB bank into place
via `0x7FFD`, then a small boot stub sets the interrupt mode before starting — so
it does not depend on esxDOS just to run.

```bat
run-zx128.bat        REM dev convenience: boots nexthack128.tap in ZEsarUX (--machine 128k)
```

`run-zx128.bat` launches **ZEsarUX** (sibling `..\ZEsarUX\`) for quick local
testing; it inserts the tape with `--tape`, so ZEsarUX auto-loads it straight to
the title (no boot-menu key). It also passes `--noconfigfile` so the shared
ZEsarUX config can't force the Next machine over the 128K.
Save/restore on the 128K needs an **esxDOS/DivMMC** interface, which the game
probes for at startup: with one, `S` writes `nexthack.sav` and it reloads on the
next boot; without one the game runs normally but cannot save. The build also
emits a `nexthack128.sna`, but it is **dead** — it boots the resident title then
crashes on the first banked call (it doesn't carry the code-banked RAM banks),
in ZEsarUX too. **Run and ship the `.tap`, not the `.sna`.**

## Controls

| Key                       | Action          |
|---------------------------|-----------------|
| cursor keys / `h j k l`   | move ◄ ▼ ▲ ► (hold to keep moving) |
| `y` `u` `b` `n`           | move diagonally |
| `>` `<` or `Enter`        | stairs down / up |
| `.` or Space              | wait a turn |
| `s`                       | search the ground for nearby hidden traps |
| `,`                       | pick up the item under you |
| `i`                       | show inventory |
| `d`                       | drop an item on the floor (sells it inside a shop) |
| `w` / `W`                 | wield weapon / wear armor |
| `P`                       | put on a ring |
| `q` / `e` / `r`           | quaff potion / eat food / read scroll |
| `t`                       | throw a weapon in a direction |
| `p`                       | pray to your god |
| `E`                       | engrave Elbereth in the dust (wards off monsters) |
| `S`                       | save game and quit to the title |
| `?`                       | show the full command list |

Walk into a monster to attack it; walk over gold to pick it up.

![The NextHack inventory screen, listing carried weapons, armour, potions, food and a scroll, with the worn and wielded items marked.](docs/inventory.png)

*The inventory screen (`i`): each item is a record of type, enchantment and erosion — the worn armour and wielded weapon are flagged.*

## Map & item tiles

The world is drawn as colourful 8×8 pixel-art tiles, not ASCII text. The tiles and
the entities they depict (the symbol in parentheses is the internal map code, kept
from the roguelike tradition):

- **Terrain:** floor (`.`), corridor (`#`), wall (`-` `|`), door (`+`),
  stairs up/down (`<` `>`), altar (`_`), a sprung trap (`^`)
- **Items:** gold (`$`), weapon (`)`), armor (`[`), potion (`!`), food (`%`),
  scroll (`?`), ring (`=`), the Amulet of Yendor (`"`)
- **Creatures:** hero and shopkeeper (`@`), rat (`r`), bat (`B`), acid blob (`a`),
  kobold (`k`), dog (`d`), snake (`S`), orc (`o`), zombie (`Z`), leprechaun (`l`),
  yellow light (`y`), homunculus (`i`), wraith (`W`)

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
- ZEsarUX emulator: <https://github.com/chernandezba/zesarux>

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
