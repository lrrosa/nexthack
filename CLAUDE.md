# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

NextHack is a NetHack-inspired roguelike written in C, built with **z88dk** (the
`zsdcc`/SDCC compiler) and tested in the **ZEsarUX** emulator. It is a fresh
engine on NetHack's design, not a recompile of NetHack's source — sized to fit
the Z80.

**One codebase builds two targets**: the **ZX Spectrum Next** (Z80N, `+zxn` —
hardware tilemap, 4bpp colour tiles, Layer 2 art, banked through the MMU) and the
plain **ZX Spectrum 128K** (`+zx` — ULA bitmap, 1-bit UDGs, a 32-column viewport
scrolling over the 80-wide map, banked through port `0x7FFD`). They share **all**
game logic; only the platform/render layer differs, selected at compile time by
`#ifdef __ZXNEXT`. **A change is not done until it is verified on both** — most
of the gotchas below are one target behaving differently from the other.

**Project status & roadmap:** see `CHANGELOG.md` (one entry per release) and
`git log --oneline` — each commit is one phase. **1.0.0 shipped 2026-07-31**, so
the line is 1.x.y under SemVer now; the 0.x.y tags are the pre-1.0 history. Start
a session from this folder so this file is auto-loaded.

## Build & run

The z88dk SDK and the ZEsarUX emulator live one directory up (`..\z88dk`,
`..\ZEsarUX`); they are not in this repo. **The build needs a nightly z88dk**
(`..\z88dk`, the v2.5-dev line, 2026-07 or newer; zsdcc base SDCC 4.5.0) for
its `__banked` trampoline — the game is **code-banked** (see Memory budget).
Verified: a nightly swap-in rebuilds clean with identical resident sizes (small
codegen drift in the banked pages is normal). Build/run are scripts run from
this folder:

```bat
build.bat            REM builds the whole Next game (forwards to build.ps1)
build.bat foo.c      REM builds a single .c file -> foo.nex
run-next.bat         REM runs nexthack.nex in ZEsarUX (Next; esxDOS auto-mounted, so 'S' save works)
run-next.bat foo.nex REM runs a specific .nex
run-zx128.bat        REM runs nexthack128.tap in ZEsarUX (128K; auto-loads the tape)
```
```powershell
.\build.ps1          # incremental + parallel Next build -> nexthack.nex (preferred)
.\build.ps1 -Clean   # force a full rebuild
.\build-zx128.ps1    # builds the ZX 128K target -> nexthack128.tap
```

**Both targets run in ZEsarUX** (`..\ZEsarUX`, ZRCP on TCP :10000) -- CSpect and
the SD-image dance are gone. The 128K MUST be the **`.tap`**: appmake's `.sna`
boots the resident title but crashes on the first banked call (it doesn't carry
the code-banked RAM banks). ZEsarUX auto-mounts esxDOS onto the .nex's folder, so
save/restore works with no SD image (the old `run-sd.bat`, now removed).

The scripts set `ZCCCFG`/`PATH` (to `..\z88dk`), compile each `.c` to a `.o` and
then link:

```
zcc +zxn -clib=sdcc_iy -SO3 --max-allocs-per-node200000 -pragma-include:zpragma.inc <bank flags> -c src/foo.c -o src/foo.o
zcc +zxn -subtype=nex -vn -clib=sdcc_iy -startup=31 <CLIB heap sizes 0> -pragma-include:zpragma.inc -m <objs> -o nexthack -create-app
```

**The runtime is `-startup=31`, not 1** (1.8): startup 1 is the same CRT plus
z88dk's stdin/stdout terminal drivers, which the game never used -- it draws its
own tilemap/ULA cells and reads keys through `in_inkey`. With them went the stdio
and malloc heaps (`-pragma-define:CLIB_*=0`; file I/O is `esx_f_*` direct and
nothing allocates): 3.8 KB of the resident half on both targets. So `printf`,
`getchar` and `malloc` do not work in this game -- nothing is wired to them.
Those flags live on the link line in the build scripts, not in `zpragma.inc`,
because an edit there recompiles every module for a link-only setting.

`zpragma.inc` (`REGISTER_SP=0xBFF0`, `CRT_APPEND_MMAP=1`, `CLIB_BANKING_SEGMENT=3`)
and `mmap.inc` (the `PAGE_20/22/26/28_CODE` section ORGs) drive the banking;
`<bank flags>` are the per-module `--codeseg`/`--constseg` read from `banks.json`
(below). `build.ps1` **skips untouched modules and parallelises** across cores:
clean ~75s, one-module edit ~25s, no-op ~2s. `build.bat` forwards the full build
to it — per-module segment flags mean the old single zcc pass can no longer
express the banking — and keeps its own single-file `build.bat foo.c` mode.

### Bank assignment lives in `banks.json`
Which bank a module's **code** and **consts** go in is declared once per target
in `banks.json` at the repo root, with the reason for each placement. `banks.ps1`
turns it into zcc's `--codeseg`/`--constseg`, and both build scripts read it. The
sources therefore carry **no `#pragma codeseg/constseg`** — an in-file pragma
silently *overrides* the command line (verified), so re-adding one takes that
module out of the manifest's control.

- Moving a module between banks is **one edit in `banks.json`**. A `.seg` stamp
  written next to each `.o` records the flags it was built with, so exactly the
  re-banked module recompiles (~25 s), not the tree.
- No `code`/`const` key = **resident**. `code` alone banks the code and leaves
  the string/const literals resident; adding `const` banks those too.
- The build **refuses an undeclared module** (it would land resident and silently
  eat stack-floor headroom) and checks the `colocate` groups: modules that hand
  consts or literals to each other's code must share one bank. That is the one
  bank mistake no size check can catch — it compiles, links, passes the 16 KB
  guards, then reads garbage at runtime with the bank paged out.

When adding a new `.c` module, put it in `src/`, add it to `$srcs` in `build.ps1`
and/or `$csrcs` in `build-zx128.ps1`, declare it in `banks.json` for each target
that builds it, and decide resident vs banked (see Memory budget).

### Where a module should go: `python tools/bankpack.py`
`banks.json` made *moving* a module one edit; `bankpack.py` decides **which bank**
instead of choosing by hand under pressure (that is how the 128K got two banks at
26 and 42 free bytes while two others held 21 KB between them). It reads the
`.o` files with `z88dk-z80nm`, so every size is what the linker will actually
place — build first; a missing `.o` makes it refuse rather than estimate.

```bash
python tools/bankpack.py plan zx128 --grow monster_ai=2000
```

- `measure` — per-module banked footprint. `report` — the current packing + slack.
- `plan` — the **fewest moves** that make everything fit (each move costs a
  recompile *and* a fresh emulator verification, so minimising moves beats
  minimising banks). `--grow mod=BYTES` asks the real question: a bank is about to
  overflow, what is the smallest fix? `--free BANK_4` asks the other one: give that
  bank headroom *now*, before it overflows -- and it keeps the plan whose tightest
  **touched** bank (the freed one and every destination) ends roomiest, so it never
  just moves the shortage. Its first version took the first bank with room and,
  asked to free the 128K's BANK_7, would have left BANK_1 at 123 B. Hot code still
  goes uncontended first. `--consolidate` packs into the fewest banks and prints
  the churn it costs. After 1.8.0 the search is exact: a move may come out of a
  bank that fits, to make room (its first version only emptied the full banks,
  and called a two-move swap a wall, #17); "NO PLACEMENT FITS" now means proven,
  and a search that runs out of nodes says so instead. `bankpack.py selftest`
  (CI) checks it against brute force.
- `compress` ranks every module by how well its banked bytes pack, using the
  game's own LZ. A **hint, not a plan**: only data read once into a destination
  that already exists can spend it — code never can, so a well-packing
  `code only` row is a mirage. It is what found the templates at 13%.
- On the 128K it also knows the **contended** banks (1/3/5/7, where the ULA steals
  cycles): `banks.json` marks per-turn modules `"hot": true` and a hot unit is
  steered to an uncontended bank when one has room. A preference, not a rule —
  `nexthack` itself sits in contended BANK_3 because its group has nowhere else.
- `apply` — writes that plan into `banks.json`, and is the only subcommand that
  writes anything. The edit is surgical (one line per moved module), so the
  hand-written `why` text survives — which the tool then tells you to fix, since
  it now describes the old bank. **Rebuild and re-verify in the emulator after
  applying:** every address in the touched banks moves, so a latent
  bank-discipline bug surfaces there and nowhere else.
- `tools/mktap128.py` derives the tape's bank blocks from `banks.json` too, so a
  repack that empties a bank drops that block by itself. The two failure modes it
  closes are opposite: a bank holding code but missing from the tape is absent at
  runtime (the first `__banked` call into it crashes), and a bank on the tape that
  no module was assigned to has no `.bin` for the loader to find.
- Colocate groups are packed as one indivisible unit, **including groups that share
  a module**: on the Next, `nexthack`+`classes`+`spells` and `nexthack`+the two
  Layer 2 palettes overlap, so all five move together or not at all.

**What it found (2026-08-30), and what was done about it.** Nothing was badly
packed: everything fitted, and the binding constraint was not free space. The wall
was the `nexthack` colocate group — one **indivisible** 16358 B unit on the 128K,
26 B under a full bank. No packing can help a unit that already sits alone in its
bank, so the only lever left was the other one: **`nexthack_lvl.c` was split out of
`nexthack.c`** (2026-08-31), taking the cold level half. BANK_3 went 26 B → 2229 B
free and PAGE_22 414 B → 2592 B. On the Next the new module took its own bank 15
(PAGE_30) instead of PAGE_20, which it would have squeezed to 344 B — the Next has
spare banks, so the split buys headroom on both targets instead of moving the
shortage from one bank to another. That is the shape of the next such fix too:
when a colocate group fills a bank, split a module or break the group (reach the
data through a `__banked` accessor instead of a pointer); never relocate.

**`item.c` was the second (2026-09-21)**: alone in its bank at 163 B (Next) and
240 B (128K), it gave up the use-verbs to `item_use.c` and went to 4376 / 4465 B
free. It is the first split that needed the accessor half of that rule. Its
catalogue is const-banked in `item.c`'s bank, so `item_use.c` may never hold a
pointer into it: what crosses is **values** -- `item_obj_prop(otyp)`,
`item_obj_cls(otyp)`, a slot, a class char. The item menu is the instructive
case. Its callers passed their prompt as a string, which after the split would
have been a pointer into `item_use.c`'s bank read with `item.c`'s mapped; since
each class had exactly one prompt, `select_item(cls)` now derives it itself and
only the char crosses. Two more habits worth keeping: the shared definitions
went into a private `item_int.h` rather than `item.h` (a `#define inv` in a
header half the game includes would rewrite any local of that name), and the
helpers `item.c` calls a lot got thin `__banked` wrappers instead of becoming
banked themselves -- a `__banked` call is the long trampoline form even inside
one bank, and `recompute_gear` alone has 14 internal callers.

**And again (2026-10-23)**: back at 157 / 37 B with a take-off command, a
second ring hand and stacks still to come, it gave the equipment verbs (`w`
`W` `P` and the amulet of life saving) to `item_wear.c` -- 2130 / 2046 B free.
Same rule, four more accessors (`item_obj_slot`, `item_gear_eff`,
`item_worn_with`, `item_obj_desc`; the last hands back obj_desc's buffer,
which is resident BSS). It cost 109 B of trampoline on the 128K and left
BANK_4 there at 277 B, which `T` then overflowed by 18. bankpack's fixes all
moved a module into BANK_0 (the room just made for item.c) or the AI's
BANK_6, so the split's line moved instead: the two choices that *scan* the
catalogue (`item_find_best`, `item_find_best_gain`) went back beside it,
where each item weighed is an array read rather than three trampolines, and
BANK_4 kept 596 B against BANK_0's 1409. The verbs act; item.c answers
questions about the catalogue. `balance.py` reads the `SL_*`
slots out of `item_int.h` now, and refuses to run without them.

**`nexthack.c` a third time (2026-10-25)**: three small features in one day
(the 128K dog's heel, the coloured HP, the passed doors) took the 128K's
BANK_3 from 283 to 132 B. The map cursor went to **`farlook.c`** --
`look_at`, `cursor_pick`, `do_farlook`, 1684 B on the 128K and 1623 on the
Next -- and BANK_3 has 1748 B, PAGE_22 1954. It was picked over the help and
score screens because nothing it touches points into the group: its literals
go to the resident `msg`, `floor_item_desc_at` hands back obj_desc's resident
buffer, and the three-word `align_name` was copied rather than shared. The
screens both print `class_name()`, a pointer into classes.c's consts, so each
would have needed a printing accessor left behind in the group. `hero_teleport`
stayed -- every teleport's one door -- and calls `cursor_pick` `__banked` with
pointers to its own locals: those may cross, because the stack is resident.
The 128K paid for it: BANK_6, the only bank with 1.7 KB to give, fell from
2083 to 399 B, and `bankpack --free BANK_6`'s one answer is farlook straight
back into BANK_3. Its six banks hold ~4.9 KB between them now, so the next
split there has to find its room before it can be made.

There are no automated *behaviour* tests, and CI does not add any. Verification is
manual: build, then run in ZEsarUX and observe. The build agent **can** drive
ZEsarUX itself over ZRCP (read memory, inject keys) to verify most behaviour; the
human confirms the wall-clock *feel* (which ZRCP's CPU-pausing reads distort).

`.github/workflows/checks.yml` guards the **invariants** instead — no toolchain,
no emulator, seconds to run: the generated sources in `src/` still match their
tools, `banks.json` still matches what the builds compile (declared modules,
intact colocate groups, real bank names), `balance.py` can still parse the C
tables, and the LZ streams still round-trip. Run any of them locally the same
way. What CI cannot catch is the failure this architecture actually produces --
*the bank loaded garbage* -- which is why a bank change is still re-verified in
the emulator.

**Two skills in `.claude/skills/` carry the procedures this repo keeps needing:**
- **`zrcp-verify`** — the emulator harness (`zrcp.ps1`: launch per target,
  symbol lookup from the `.map`, read/poke, key injection, message-line decode
  on both renderers) plus the trap list. Use it to prove any change; do not
  hand-roll the plumbing. It also carries `mame.ps1`, a headless MAME harness
  (scripted steps, both targets): the second emulator, and the one that loads
  the real `.tap` through the 128 ROM and reads memory without stretching time.
- **`bank-budget`** — `python tools/bankmap.py` prints the resident half, every
  code bank's free tail and the **Bank-5 tenant map with overlap detection**;
  the skill holds the relocation procedure for a full bank. Run it *before*
  adding code, data, tiles or strings, and after any change to `NTILES`,
  `FOV_SLOTS` or `MAXINV`.

### Balance: measure it, don't guess it
`python tools/balance.py report` models the difficulty curve offline (a minute,
no emulator). It exists because 1.0 shipped without the balance pass and the
questions it answers -- how much HP a fight costs at depth N, how many fights a
full HP bar buys, where runs actually die -- cannot be answered by playing.

Three rules make its numbers trustworthy, and any change to the tool must keep
them: (1) `montypes[]`, `objtypes[]` and `classes[]` are **parsed out of the C**,
never retyped, and a table that stops parsing makes the tool refuse to run
rather than report stale numbers; (2) every formula carries the `file:line` it
mirrors (`balance.py formulas` prints them side by side) -- line numbers, so they
rot whenever code above them moves (by 1.4, 37 of the 40 pointed at the wrong
line); re-point them in the commit that moves the code; (3) the dice are the
game's own xorshift16 and item hashes, bit-exact -- **one stream per simulated
life** (`fresh_rng`): until 1.6 a batch ran 600 lives on one stream, which at
~3700 draws a life wrapped the 65535-step period ~33 times, so lives were
phases of one cycle and the Valkyrie's wins read 58% instead of 67%. So **after
tuning a table, re-running the tool measures the change immediately** -- that
is the point of it.

The model plays every class the way the game lets it: since 1.6 it casts the
kit's force bolt when that beats the blade, flashes the Tourist's camera at the
big hitters from Dlvl 20, and -- the other 1.6 correction -- a sleeper is still
asleep at the first blow only for a stealthy hero, as `still_asleep` rules;
it used to grant every class a free sneak attack in half its fights. The class
targets set 2026-10-05 (NetHack's order, measured with the dog over 5 seeds):
Valkyrie ~59%, Wizard ~43%, Rogue ~35%, Tourist ~19%. Before the signatures the
three were at 0-1%; without the dog every class is under 3%. The 1.9 four were
set to NetHack's order around them (2026-10-24, same measure): Samurai ~66%,
Barbarian ~61%, Priest ~51%, Healer ~17%. Two model corrections came with
them: a Fast hero's free turn runs on a clock across fights (`hero.clock`, as
the game's `turns` does -- counted per fight it almost never came round, and
Fast was worth 0.5 points instead of 16), and a hurt hero with the healing book
casts it between fights before drinking a potion. Wisdom 14 is a cliff for a
healer-caster: it is the Pw-regen threshold, and the model's Pw arrives a whole
point per fight (68% against 16% for the Priest at Wi 14 and 13).

Subcommands: `tables curves duel gear rest gauntlet runs sweep report`
(`--class`, `--depths`, `--turns`, `--engage`, `--pet`, `--csv`). `sweep` exists
to show how much a conclusion depends on the three things the model has to guess
(turns per level, fraction of a level fought, whether the dog lives) -- quote it
whenever quoting a death depth. The 2026-08-29 findings are in the *Armour
cliff* report: defence plateaus at `armor_def` 7-8 by Dlvl 10 while the bite's
`+eff_depth()/4` grows forever, so absorption falls from 86% of blows to zero by
Dlvl 28; no monster can kill a full-HP hero one-on-one at any depth, and
nothing converted time into HP. `R` (rest, `rest_step` in `nexthack.c`) answered
that the next day, and `balance.py rest` prices it: it mends no faster than
waiting, and every rested turn rolls a wanderer, so it pays only while an
average fight costs under `wander_period / regen_period` (3.5-5 HP by Co). That
holds through Dlvl 25 for every class and fails by Dlvl 30 (Wizard, Tourist) or
40 (Valkyrie, Rogue): deeper, the wanderers a rest attracts cost more HP than
it heals.

### The 128K target must also run on RAM-expansion interfaces
The `.tap` is expected to work not only on a real 128K but on a **48K + external
RAM interface** (e.g. `tkmem128-ii`), which is how many people get 128K RAM.
Those boards implement the `0x7FFD` latch but *not* the machine around it. Two
standing rules, both already satisfied — do not regress them:
1. **Never enable the shadow screen (bit 3 of `0x7FFD`).** An external interface
   physically cannot do it (the ULA's display RAM is inside the machine), so it
   is the one documented incompatibility of such boards. We are immune because
   the renderer is dirty-cell and draws to the normal ULA bitmap; the only
   runtime writer of the port is `banked_call.asm`, whose `or 16` leaves bits
   3/5/6/7 clear. Bank 7 is therefore plain RAM to us (see `bank-budget`).
2. **Never depend on 128K-ROM system variables.** Such a board usually has no
   128K ROM, so `BANKM` (23388) is just the printer buffer full of garbage. The
   tape loader reads it to preserve the ROM-select bit and now masks it to
   **bit 4 only**: letting bit 5 through would hit the *paging lock* and freeze
   the latch before the banks finish loading. When writing `0x7FFD`, write the
   bits you need (banks 0-2, ROM 4) and force the rest to 0.

### Gotchas that waste time
- **128K: the `.tap` is the only artifact.** A `.sna` (appmake can still make one
  by hand) boots the resident title then crashes on the first banked call — it
  doesn't carry the code-banked RAM banks — so the build no longer emits one.
  `run-zx128.bat` needs `--noconfigfile` or
  the shared `.zesaruxrc` (left by `run-next.bat`) forces the Next machine and
  reloads the last `.nex` over the tape (see the `zesarux-zrcp-debugging` memory).
- SDCC's `warning 110 ... "EVELYN the modified DOG"` is a **harmless** peephole-optimizer
  message; ignore it.
- SDCC `int` is **16-bit**. Watch for overflow; `long` works but is slow.
- **A pointer plus a negative constant can assemble WRONG, with only a warning.**
  SDCC sometimes writes `p[-81]` as `ld a,+((0xffffffaf) & 0xFF)` /
  `ld a,+((0xffffffaf) / 256)`, and this z80asm clamps any literal above
  `0x7FFFFFFF` to 2147483647 -- so the offset came out as -1, not -81 (and a
  plain `(-81)/256` divides signed, giving 0 for the high byte). The BFS flood
  read the wrong wall byte and leaked through rock until ZEsarUX caught it
  (2026-09-27). The assembler's warning names `2147483647`; both build scripts
  now refuse a module whose log has one. Write such offsets from a lower base
  pointer (`base + 0/1/2`, see `FLOOD_TRY`) instead of a negative index.
- In PowerShell, `Set-Location` does **not** change .NET's cwd: `[IO.File]::ReadAllBytes`
  needs an absolute path.

## Architecture

Strict split between the **Next hardware platform layer** and **game logic**:

All source modules live in **`src/`**; the build scripts, `mmap.inc` and
`zpragma.inc` stay at the repo root (the build runs from root, where z88dk's
`CRT_APPEND_MMAP` looks for `mmap.inc`). `tools/` holds the asset converters
(title art, level templates), `docs/` the README images.

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
| `leveltmpl.c` | B | loader for the hand-drawn special-level templates (generated `leveltmpl_data.h`, LZ-packed and const-banked beside it) |
| `monster.c/.h` | R | monster arrays + per-monster leaves (`monster_at`, `mon_find`, `mon_tile`, `pick_mon`); catalogue |
| `monster_ai.c` | B | BFS chase and combat (the per-turn half) |
| `monster_spawn.c` | B | the cold third: level-entry spawning + the killed-monster mask and its save (split off when `monster_ai`'s bank filled) |
| `classes.c` | B | the class picker; banked *including* its consts and literals |
| `spells.c` | B | spellbooks and casting (`r` learns a book, `Z` casts) |
| `music.c` | B | the AY title theme — a 3-voice sequencer, not a tracker replayer; the title plays it twice through before the demo |
| `attract.c/.h` | B | the title's attract demo: ~15 s of magic-mapped levels walked to the stairs by a random class (see Title & victory screens) |
| `item.c/.h` | B | the inventory, the object catalogue and its names, the worn set's effects (`recompute_gear`), the floor and its stashes, pick up/drop |
| `item_use.c` | B | the verbs that activate or consume an item: quaff (and fountains), eat (and corpses), read, throw, zap, and `E` (engraving with a wand) |
| `item_wear.c` | B | the verbs that put gear on and take it off: `w` wield, `W` wear, `P` put on (two ring hands), `T` take off, and the amulet of life saving |
| `secret.c` | B | secret doors and corridors (1.8): hidden by side hash after `gen_level`, never cutting `<` off `>`; found by `s` |
| `feature.c` | B | the furniture (1.8): where sinks, thrones and graves stand; drinking from, kicking and dropping rings down a sink; graves' epitaphs and digging one up |
| `throne.c` | B | `#` (1.8): sitting, and NetHack's thirteen throne effects (no wish) |
| `item_int.h` | — | item.c's internals, shared only with the modules split off it (`item_use.c`, `item_wear.c`) and the furniture's: `obj_t`, `inv`, the `O_*` ids and `SL_*` slots, and the `__banked` value API |
| `sfx.c/.h` | B | beeper sound effects |
| `titlegfx0/1/2.c`, `victorygfx0/1/2.c` | B | the Layer 2 title / victory images (generated): 3×16 KB framebuffer thirds const-banked into banks 16/17/18 and 19/20/21 (see Title & victory screens) |
| `titlepal.c`, `victorypal.c` | B | each image's 9-bit palette, const-banked in `PAGE_22_CODE` next to the code that streams it |
| `scr.c` | B | **128K only** — unpacks the title/victory SCR screens onto the ULA (resident `lz_expand`); must sit in the same bank as their consts |
| `title_scr.c`, `victory_scr.c` | B | **128K only** — the generated SCR images (`tools/png2scr.py`), 6912 B each LZ-packed to ~4 and ~3 KB (1.8), banked consts |
| `puttile_asm.asm` | R | **128K only** — the ULA cell blits in hand-written Z80 |
| `esxdetect.asm` | R | **128K only** — probes for a DivMMC/esxDOS before a save is attempted |
| `banked_call.asm` | R | **128K only** — the vendored banking trampoline (the Next uses the SDK's) |
| `nexthack.c/.h` | B | game-state globals (resident DATA) + rendering, turn step, screens; `.h` declares its `__banked` entry points for `mainentry.c` (and `save.c`'s and `farlook.c`'s) |
| `save.c` | B | save & restore, split off it (2026-09-27): the file format, its length+sum trailer, the two-pass load and the prompt for a save that cannot be loaded |
| `nexthack_lvl.c` | B | the cold level half split off it: `build_level`, the altar/fountain/pet/follower placement, and the stairs |
| `farlook.c` | B | the map cursor, split off it (2026-10-25): `;` farlook (`look_at`) and `cursor_pick`, which teleport control (`hero_teleport`, still in `nexthack.c`) asks too |
| `game.h` | — | shared player/run state (`extern`s defined in `nexthack.c`) |
| `font_data.h` | — | the 8x8 font, generated by `tools/txt2font.py` from `tools/font/nexthack.txt` (see The font) |

Shared mutable state (`hero_x/y`, `dlvl`, `php`, `gold`, `ac`, `xp`, `nutrition`, …)
is declared `extern` in `game.h` and **defined once in `nexthack.c`** (as resident
DATA — banked code's data is resident too). Modules include `game.h` to read/write it.

### Display (the tilemap)
- Renders on the Next **hardware tilemap, 80×32 chars** (`TM_W`×`TM_H`). The ULA layer
  is disabled (NextReg 0x68); only the tilemap shows. Runs at 28 MHz (NextReg 0x07).
- Tile/map data lives in **Bank 5** (fixed at CPU `0x4000-0x7FFF`, free because the
  program is at `0x8000+`): tile definitions at `0x4000`, tilemap at `0x6000`
  (NextReg 0x6F/0x6E). **Tile definitions must stay below `0x5C00`** (NextZXOS sysvars).
- **Tiles 0..127 = the game's font** (`font_data[]`, expanded at boot by
  `tm_init_font`; a tile id IS its code, 0..31 blank), used for text and coloured
  per cell via the attribute's palette **offset** (see palette below). Tiles 128+
  (`T_*` in `platform.h`) are **4bpp colour graphic tiles** (`gfx[]` in `platform_init.c`,
  const-banked) for map cells. Adding a graphic tile: add a `T_*` number, an entry in
  `gfx[]`, and bump the count in `load_gfx_tiles()`. In 80-column mode every pixel is
  **half-width**, so a tile shows 1:2 (a tall cell, like a text glyph): judge art at
  that aspect -- a shield drawn square read as a tombstone. The 128K draws its own
  1-bit `udg_src[]` at 1:1, so each tile is designed twice.
- **The tilemap's transparent pixel value is 0** (NextReg 0x4C in `tm_init`). A
  transparent pixel shows the fallback colour (NextReg 0x4A, black), and index 0 is
  black in every palette offset, so that is invisible. It was the reset value 0x0F
  until 2026-10-04 -- and index 15 is skin, so every face rendered black (the
  shopkeeper had no head). Don't move it back to 15 or give index 0 a colour.
- **Palette offsets** (tilemap palette, NextReg 0x43=0x30):
  - offset 0 (indices 0..15) = full-colour **master palette** for graphic tiles in view.
  - offset 1 (indices 16..31) = **dimmed master** (channels halved) for remembered,
    out-of-sight terrain. Half is the dimmest a grey stays neutral on 3-3-2 colour.
  - offsets 2..15 = black-paper/ink pairs for **coloured text** (`inkcol[]`).
    A glyph's pixels are only 0 or 1, so entries 2..15 of each are free: offset
    3's hold the **second monster palette** (`pal2[]` in `platform_init.c` --
    purple, pink and olive, which the master palette lacks; entry 1 is the
    dim-magenta ink, a usable purple). A tile drawn there is listed in
    `tile_pal2()` (`platform.h`) and its pixel values index `pal2[]`, not
    `master[]`; `draw_map` gives it attribute `PAL2_ATTR` in the two branches
    that draw a monster. **Monsters only**: a monster is never drawn dimmed,
    while an item or a wall needs offset 1's dimmed twin, which `pal2` has not.
    Recolouring a tile into it is a `gfx[]` edit plus a `tile_pal2` entry.
  - Gold in the status bar draws a graphic `T_DOLLAR` tile instead of the ROM `$`.
    That started as a workaround for CSpect, whose ROM had no `$` glyph; the ROM
    ZEsarUX loads does have one (checked), so the tile is now only a colour choice.
    Don't repeat the claim that the glyph is missing.
- **Rendering is a single full redraw per turn** (`draw_map` in `nexthack.c`) using a
  running tilemap pointer and inline FOV bit-tests for speed; writing each cell exactly
  once (rather than clear-then-fill) is what keeps it flicker-free. Status/message
  lines follow the same write-once-then-pad rule.
- **The 128K's `draw_map` is three paths over a 32-column viewport**, each writing a
  cell only where it differs from `VIEW_SHADOW` (what is on screen): fast (only the
  hero and monsters moved), mid (repaint the cells whose vis bit flipped, by XOR
  with `PREV_VIS`) and full. The full path's terrain sweep is `dm_row`
  (`puttile_asm.asm`), a row at a time, after `draw_map` has painted the monsters and
  the hero and marked them in `MON_MAP`. Only `map_dirty` (an overlay closed, a new
  level) **forces** every cell; an edge-scroll keeps the shadow and repaints what the
  new origin changed -- forcing it, and the C sweep, made each scroll a 0.6 s stall
  (now ~0.1 s). A new way to write over the map without setting `map_dirty` would
  leave the shadow lying, on every path.
- **`udg_ink[]` (`platform.c`) is each 128K tile's whole attribute, not just an
  ink**: `ink | paper<<3`, and every map path ORs BRIGHT (0x40) for a cell in
  sight. Since 2026-10-11 four tiles have a paper -- walls white on blue (15),
  doors black on red (16), shop walls yellow on red (22), the fountain cyan on
  blue (13); the mines' walls stay yellow on black so Minetown's shop stands
  out. A new path that draws a map tile must take the byte whole (never `& 7`),
  and a door's colours must read in all three walls it can sit in.

### The font (1.7)
Both machines print with one 8x8 font, not the ROM's: **Byteletter** from
DamienG's ZX Origins collection, modified. Its glyphs are art in
`tools/font/nexthack.txt` (each `@ <hex code> <char>` then 8 rows of `#`/`.`,
the changes listed in its header); `python tools/txt2font.py` writes
`src/font_data.h`, and CI checks it is current. `platform_init.c` reads it once:
the Next expands it into tiles 0..127, the 128K copies it to `font_ram`, where
`putcell` (`puttile_asm.asm` `FONT_RAM`, exported as `_font_ram`) reads code c at
`FONT_RAM + (c-32)*8`.
- **Credit and licence**: "Font based on Byteletter by DamienG", in the README's
  Credits. Free in games; its licence forbids redistributing it *as a font*, so
  never publish the glyphs as a font file (a `.ch8`, a TTF) -- the game's own
  sources are fine.
- **Legibility is the test, on both machines**: an 8-pixel glyph is half-width on
  the Next and square on the 128K. Byteletter's own Fraktur capitals, its `l`/`1`
  and `i`/`j` pairs and its hairline punctuation failed that test and were
  redrawn; preview a change on real screens (dumped over ZRCP, re-rendered).
- **The 128K's text is read through the font**: `zrcp.ps1`'s `Get-MsgLine`,
  `mame.lua`'s `msg` and the fuzzer's cursor check match screen cells against
  `_font_ram` (or the ROM's `0x3D00` for a build without it). A test that compares
  text against hard-coded ROM bytes is wrong now.
- `0x5D00-0x5FFF` was proven free by a save and restore through esxDOS leaving
  the copy intact; the 48K ROM's IM1 keeps to the system variables below
  `0x5CB6`, and the game never returns to BASIC or prints through RST 16. A new
  use of a ROM routine that touches the BASIC workspace would overwrite the font.

### Title & victory screens (Layer 2)
The **only** use of the Next's **Layer 2** framebuffer (256×192, 8bpp); everything
else is the tilemap. `title_screen()` shows the loading image; `victory_screen()`
shows the win image (both in `nexthack.c`), then the game switches back to the
tilemap.
- The images are **generated**: `tools/png2layer2.py` (Pillow) converts each
  source in its `IMAGES` table — `tools/title.png` → `titlegfx0/1/2.c` (framebuffer,
  row-major `y*256+x`, three 16 KB thirds) + `titlepal.c`, and `tools/victory.png`
  → `victorygfx0/1/2.c` + `victorypal.c`. `python tools/png2layer2.py [name]`
  regenerates one or all. It picks the path by source size: a **256×192** source
  (hand-edited final-res art) is packed **pixel-exact** (each pixel snapped to the
  nearest Next 9-bit RGB333 colour, palette = the distinct colours — no resampling);
  a larger render is resized to 256×192 and quantized (`MAXCOVERAGE`, so small
  saturated areas keep a slot). To edit the art, edit a 256×192 PNG in the Next
  palette and re-run the tool; the generated `.c` files **are** committed.
- The thirds are **const-banked** (title 16/17/18, victory 19/20/21) so the `.nex`
  loader writes them where Layer 2 reads them **in place — no runtime copy, zero
  resident cost** (banks outside the CPU window). The shared `show_layer2(pal,bank)`
  streams the palette (NextReg 0x43/0x40/0x44), points Layer 2 at the image's first
  bank (NextReg 0x12), turns the tilemap off (0x6B=0) + Layer 2 on (0x69 bit7);
  `hide_layer2()` reverses it (0x69=0, 0x6B=0xC0). Each palette **must** live in
  `PAGE_22_CODE` (bank 11) because the code that streams it runs from there.
- **The attract loop** (`title_screen`, both targets): the title plays the
  whole theme `TITLE_PLAYS` (2) times (`music_title_wait(plays)` returns 0 when
  it runs out), then `attract_demo()` (`attract.c`) shows about 15 s
  (`DEMO_FRAMES`) of consecutive levels of a random world -- magic-mapped, a
  random class with its kit on, the dog at heel, walking to `>` down
  monster_ai's own chase field flooded from the stairs, under a blinking DEMO
  line -- then the art returns. The 128K times the showing by FRAMES; the Next,
  which has no frame clock the code trusts, by a count of the frames it waited
  plus a per-step estimate calibrated under ZEsarUX. A key at either begins the
  game and seeds the world; the demo also reads the ROM's key latch (FLAGS
  bit 5), so a tap during a slow redraw is not lost. The
  demo draws with the real `build_level`/`fov_update`/`draw_map` on the real
  globals, which is safe only because the title is never reached with an
  unsaved run in RAM (boot, or after `S`); it never calls
  `try_move`/`monsters_turn`/`upkeep`, so no mask, pack or conduct moves, and it
  puts back `dlvl`, `max_dlvl`, `turns`, `hero_face` and `st_blind`. Since
  2026-09-27 `main()`'s fresh start is a whole `new_game(0)`, so a failed
  restore after `S` (or after the demo) can no longer begin a "new" game on
  the saved run's depth with its gold and kill masks; `hero_face` is the one
  value there that `new_game` does not set.
- **Two banking gotchas this exposed:** (a) a translation unit gets **one** const
  section, so each bank's array needs its **own `.c`** (hence three files); which
  bank that is comes from `banks.json`, not from a pragma in the file. (b) z88dk
  **predefines** `BANK_nn` sections already ORG'd at `0x__C000` (bank 16 = 0x20C000,
  i.e. `(page8k<<16)|0xC000`), so reference them from `constseg` but do **not**
  re-`ORG` them in `mmap.inc` (that errors "ORG redefined").

### Level generation, persistence & FOV (`level.c`)
- The map is a `char lvl[MAPH][MAPW]` grid (`'.'` floor, `#` corridor, `-`/`|` wall,
  `+` door, `<`/`>` stairs, `$`/`)`/`[`/`!`/`%`/`?`/`=`/`"` items, `' '` rock). Monsters are
  **not** in this buffer (they live in `monster.c` arrays and are drawn on top).
- **Persistence is deterministic**, not stored maps: `gen_level()` calls
  `rng_set(level_seed(dlvl))` so a given depth always regenerates identically. Player
  changes (gold taken, monsters killed, items picked up) are kept in tiny per-depth
  **bitmasks** and re-applied after regeneration -- and the pick-axe's tunnels as a
  list of cells (`dug_pool`, see Items), re-opened last. **Do not change the order/number of
  `rn2()` calls inside generation casually** — it changes every level and can desync
  the persistence bit indices.
- `build_level()` (in `nexthack_lvl.c`) orchestrates: `gen_level()` → the
  door list → the secrets (1.8) → spawn monsters → altar, fountain and
  furniture → apply gold/monster/item persistence → the dug cells → the passed
  doors → the floor stash. Gold/items are placed before monsters so spawning sees
  the same map each visit, and anything that decides by looking at the map must
  run before the persistence: until 2026-10-10 the altar and fountain came after
  it, so a pile taken from the room's centre made one appear on the next visit.
- **Special levels** (Phases 22-24, `levelgen.c`): certain depths are landmark
  levels, decided by **side hashes** (never `rn2`, so ordinary levels stay
  byte-identical and persistence stays in sync). `special_gen()` at the top of
  `gen_level` fully *replaces* a level — the **Big Room** (every 11th depth) is one
  giant lit chamber filling the playable area (`rcount=1`, room in `r_*[0]`), and a
  **hand-drawn template** (≈1/9 of depths ≥ 3) stamps a fixed map. The **treasure
  vault** (some depths ≥ 4, in the loot block, precedence over shops) instead
  *augments* a normal level: it packs a **leaf room** (one door, never a
  through-route, holds no stairs — so filling it never splits the map) with gold +
  items, `monster_ai.c` posts tough guards in it (the first few spawns, kept ≤
  `MAXMON`), and `item.c` resolves its loot at a deeper effective depth (`dlvl +
  VAULT_DEPTH_BONUS`) for richer gear. `level_vault_room()` / `in_vault_room()`
  expose it to the spawner and to `item.c`.
- **Secret doors and corridors** (1.8, `secret.c`): one door in eight
  (`door_hash` `(h & 7) == 2`, nexthack.c) and about one straight corridor
  square in 64 are hidden as wall or rock, on ordinary levels from Dlvl 2. One
  four-way flood from `<` per level marks a way to `>` (the Amulet's cell on
  50) and to the mine entrance on Dlvl 2, and nothing on that way hides -- so
  the stairs stay joined by construction. The user chose that over NetHack's
  rule, which can wall you in; the fuzzer's walk check and the attract demo
  rely on it. It costs the 128K ~80 ms a level entry (0.24-0.29 s became
  0.32-0.39 s, MAME, one seed); a flood per candidate had cost up to 0.75 s,
  and a C loop over all 1482 cells 70 ms, which `memchr` (CPIR) replaced. The
  flood itself cost 50 ms in C and 23 in the 128K's Z80 (`lw_run`, inline in
  `secret.c` so it stays in that module's bank; 5.6 ms of it is clearing the
  field). The Z80 is the Next's C `label_way` step for step and leaves the
  same labels byte for byte -- proved on 3000 generated maps under
  z88dk-ticks and on 60 real levels in MAME -- so **a change to the flood's
  rules must be made in both**. For its length the targets carry a 0 in
  `lvl[]` (a char no map holds), put back before `label_way` returns. A door
  is locked where `(h & 3) == 0`, so none is both and the locked doors'
  map-order index never shifts when one is found. The flood borrows
  `dist[]`/`bfsq[]`, which no reader trusts without reflooding; the 128K
  lays that scratch out its own way (the ring as byte planes at 0x7400 and
  0x7500, the labels at 0x7600). `s` finds one in five tries (luck 3 or 9); found ones ride the
  dug pool (below).
- **Passed doors** (2026-10-25, `levelgen.c`): a door the hero has walked
  through is drawn as an empty doorway (`CH_DOORWAY`, the floor tile) on every
  visit after, as NetHack's opened doors stop looking shut. Only the drawing
  changes: it stays opaque and a door to every rule, so **a door test is
  `IS_DOOR(c)` (`level.h`), never `c == '+'`** -- `'+'` alone only where a door
  can still be shut (locked, kicked, keyed, the wand of opening). The locked
  doors' map-order index counts both (`door_hash_locked`), or a passed door
  would shift every later locked door onto another's forced bit. Remembered as
  the gold is: `door_note` lists the doors in map order straight after
  `gen_level`, before `secrets_hide` walls any up, so a secret door keeps its
  index found or not; `door_passed[dlvl]` holds a bit per listed door
  (`DOOR_MAX` 16 -- every ordinary level measured has 14, a template 8-10; past
  it a door just stays shut); `door_pass` (from `try_move`) sets one, and
  `door_restore` reopens them after `dug_restore`, so a found secret door is a
  `+` again first. 143 B resident; saved last before the trailer
  (`SAVE_VER` 39), so a 38 save loads with every door shut.
- **Furniture** (1.8, `feature.c`/`throne.c`): a sink (`~`, one level in 7), a
  throne (`\`, one in 10 from Dlvl 5) and a grave (`` ` ``, likelier deeper),
  each on a fixed interior cell of a hashed room if it is plain floor outside a
  shop -- placed beside the altar and fountain, before the persistence, and not
  on special levels or in the mines. Those chars exist only in `lvl[][]`: every
  renderer draws `tile_for`'s tile. `floor_drop` accepts them, so an item can
  rest on one as on an altar. What changes for good rides the dug pool:
  a vanished throne or sink and a dug-up grave are `DUG_FLOOR`, and a sink that
  gave up its ring a `DUG_MARK`. Two-part news uses `msg_more` (`platform.c`),
  NetHack's --More--; the 128K borrows the right end of row 1 for the prompt
  and repaints it from `VIEW_SHADOW`.
- **Shops are specialised** (1.8, `levelgen.c` `kind_of_shop`): NetHack's ten
  kinds by its odds, a side hash of world and depth (Minetown keeps a general
  store). A general store stocks exactly what it did; a specialist buys only
  its own classes (`shop_buys`), and walking in names it.
- **Templates** (Phase 24): hand-drawn 21×80 ASCII maps in `tools/templates/*.txt`
  (with a `;rooms:` metadata line) are packed by `tools/txt2template.py` into the
  generated `src/leveltmpl_data.h` — `const` grids + room rects, const-banked with
  the loader (`banks.json` says which bank; `load_template()` runs from that same
  one, so it reads the const in place). It stamps the grid into `lvl[][]`, finds
  `<`/`>`, and fills `r_*[]`/`rcount` from the metadata so FOV lights the chambers.
  To add or edit a template: edit a `.txt`, re-run the tool; the generated `.h` is
  committed. There are 6 templates (cavern/crypt/fortress/maze/temple/oracle).
- **The grids are LZ-packed** (`tools/lztmpl.py`): 8400 B of ASCII becomes 864 B,
  because `lz_expand()` unpacks each stream straight into `lvl[][]` — the 1680
  bytes `load_template` was going to write anyway, so the destination is free and
  the bank keeps the difference. `leveltmpl` costs 1459 B instead of 8801. That is
  the general rule for compressing anything here: **only where the thing it
  unpacks into already exists and the data is read once** (`python
  tools/bankpack.py compress` ranks the candidates, with the same caveat).
  The generator refuses to emit a stream that does not round-trip, and a 6th
  template now costs ~170 B rather than a bank. **`lz_expand` is resident**
  (`platform.c`, 1.8) so it reads its stream from whichever bank the caller runs
  in: the 128K's title and victory SCRs took up the same format then (unpacked
  straight onto the screen, which is their window), and that freed the 6.7 KB
  of BANK_4 the furniture needed. `tools/lztmpl.py`'s `expand()` is its model.
  **Any early-returning special level must rely on `gen_level` resetting
  `shop_room`/`vault_room` at the top** — `special_gen` skips the loot block, so
  without that reset a special level inherits the previous level's shop/vault.
- **Win condition**: the deepest level (`DLVL_AMULET`, currently 50) has no
  down-stairs — the Amulet of Yendor (`"`) sits on that cell instead. Picking it up
  sets `has_amulet`; climbing `<` on Dlvl 1 while carrying it sets `won` (victory
  screen, then restart). The amulet is placed **without RNG** (on the would-be
  down-stairs cell), so it cannot desync the deterministic per-depth generation.
  **No way down may exist on `AT_BOTTOM` levels** (Dlvl 50 and Mine:4, `game.h`):
  no `>`, digging is refused, and a trap door hashes into a dart trap there. A
  trap door on Dlvl 50 used to drop the hero into internal level 51 = Mine:1,
  one climb from Dlvl 2 with the Amulet. `IN_MINES` has no upper bound, so any
  new way to change `dlvl` must keep it inside 1..54.
- **FOV** remembers explored cells in an **LRU pool** (`fov_pool`: the `FOV_SLOTS`
  most recently visited levels' 1-bit-per-cell maps; entering a new level evicts and
  forgets the least-recently-used one) plus a recomputed-each-turn `vis_now` bitmap.
  Visibility = the hero's room (lit on entry)
  + radius 1 + line-of-sight rays down corridors (walls/rock/**doors are opaque**, so
  rooms are only revealed on entry). `draw_map` shows unseen=black, in-sight=full,
  seen-but-not-visible=dimmed.

### Save / restore (`save.c` + per-module `*_save`/`*_load`)
- **Model**: NetHack-style *save & quit*. `S` writes the whole game to
  `nexthack.sav` (a magic+version header, the player struct, then each module's
  state, then a 4-byte trailer: the count and 16-bit sum of every byte before it)
  and returns to the title; the boot path calls `load_game()`, which restores and
  then **deletes** the file (no save-scumming), else starts fresh.
- **Nothing is trusted until the whole file is.** `load_game` first reads the file
  through once (`save_check`) and compares length and sum with the trailer; only
  then does a second pass read into the live globals. A short or damaged file
  gets the same y/n as another version's (`save_refused`), and on `n` stays on the
  card. On the save side `file_write` latches a short write and `file_close` a
  failed flush (`file_bad`, `platform.c`); a failed save deletes its stump and
  reports, and the run in RAM plays on. There is no temp-file+rename: the load
  consumes the file, so at `S` time there is no older good save to protect.
- The **play RNG** is saved (`SAVE_VER` 30) and `build_level` sets it aside
  around the deterministic part (`gen_level` reseeds per depth; the spawns that
  follow draw from that stream). Before, every level entry and every restore left
  the play dice at the same per-level point.
- The *current* level is **not** saved: `build_level()` regenerates it
  deterministically from the restored `world_seed` + persistence bitmasks, exactly
  as a revisit does. Saved state = `world_seed`, the player globals, the inventory
  (`item.c`), and the per-depth `gold_taken`/`item_taken`/`mon_dead` masks plus the
  `fov_pool` LRU fog-of-war, the pick-axe's `dug_pool` and the genocide mask
  (`level.c`/`monster.c`), the passed doors (`door_passed`, 39) and the
  engraving (`el_x`/`el_y`/`el_life`, 40).
- **A restore is a level change that must not act like one.** `load_resume`
  (`save.c`) runs what follows `load_game`: `build_level`, which wipes the
  engraving as any level change must, so the saved one is put back after it
  (#25: a burned Elbereth vanished on save and restore); then any monster the
  level spawned on the hero's square steps aside, by position only, so neither
  the generation nor `mon_dead` notices (#24: save on a lured-away monster's
  spawn square and it came back on top of you); then the dog. Anything else
  `build_level` resets for a new visit and a save should keep belongs there.
- **Any save-format change bumps `SAVE_VER`, and an older save gets the
  incompatible-save prompt** (1.3.1): name it, ask, and leave it on `n`. The
  identification bitmap is `(NUMOBJ+7)/8` bytes, so appending catalogue types
  breaks the format every time `NUMOBJ` crosses a multiple of 8. A change that
  keeps the bytes but writes something an older binary cannot read -- a new
  `otyp` in the pack, which it would look up past its catalogue -- still bumps
  `SAVE_VER` and leaves `SAVE_VER_MIN` where it was: the older save loads, and
  the older binary sees another version and asks (36, the two-handed sword).
- File I/O lives in the **platform layer** (`file_*` in `platform.c`, wrapping
  esxDOS `esx_f_*`). It needs a mounted writable filesystem, which **`run-next.bat`**
  gives for free: ZEsarUX auto-mounts esxDOS onto the .nex's folder, so `S` saves to
  `nexthack.sav` there and the next boot restores it. (No SD image / `hdfmonkey`.)
- Memory budget: the cheap per-level masks scale to every level (`MAXLVL` = 54,
  the 50 floors plus the four mine levels, ~5 bytes/level), but the fog-of-war does **not** — it is a fixed
  `FOV_SLOTS`-entry LRU pool, so RAM is independent of dungeon depth. esxDOS itself
  adds ~1.5 KB of BSS (sector buffers), so `FOV_SLOTS` (12) is kept below the RAM
  max (~18) to reserve headroom for future features.

### Items (`item.c`)
- The inventory is an `obj_t[]` (an `otyp` into the `objtypes[]` catalogue, an
  `ench` enchantment, an `ero` erosion level, a `worn` flag, the BUC bits and,
  since 1.9, a `qty`) — up to `MAXINV` (26, one per menu letter a..z), drawn in
  two columns on the inventory screen.
- **Stacks** (1.9, `stacks_with` in `item.c`): potions, scrolls, food (a
  corpse only with the same monster's) and daggers merge when otyp, ench, ero
  and the whole BUC byte match -- a blessed potion you know is blessed stays
  apart from one you have not looked at -- up to `STACK_MAX` (99). `inv_add`
  merges before it checks for a free letter, so a full pack still takes a
  potion it holds; `floor_drop` merges too, so daggers thrown one way pile up.
  Using one (`item_use_one`: quaff, eat, read, throw, offer) shrinks the stack;
  `d`, a sale (`qty` x half price) and a nymph take the whole entry. **Every
  `obj_t` built anywhere must set `qty`** -- an uninitialised stack local is a
  garbage count -- and the floor still resolves one object per cell. `obj_t`
  is 6 bytes now, and `tools/bankmap.py` counts it out of `item_int.h`: the
  128K's `inv[]` ends 4 B under `dist[]`, so its next byte moves `inv[]`.
- The floor only stores an item's **class char** (`)` `[` `!` `%` `?` `=`), so
  generation/persistence stays untouched. The *specific* object (which weapon,
  what enchantment) is resolved when the cell is looked at or picked up,
  deterministically from `(dlvl, x, y, world_seed)` via a side hash that does
  **not** touch the RNG stream — so a floor item is always the same thing and
  stays in sync with the deterministic generation. Better/enchanted items appear
  deeper (`mindep` per catalogue entry).
- **Generation is weighted** (`prob` per catalogue entry; `resolve_otyp` draws in
  proportion within the class). A class whose types all weigh 1 resolves exactly
  as `h % n` — weapons and armour stay that way, so the combat ladder is
  untouched by new types elsewhere. **When you add a type, choose its weight so
  the existing ones keep their share, and measure it** with `tools/balance.py`:
  the healing potions weigh 3 against 2 because equal weights cut their share
  from a third to a quarter, which alone halved the Valkyrie's wins. A type
  stronger than everything it joins needs the measuring most: full healing
  (1.6) refills the whole bar, and at one potion in 19 from Dlvl 10 it took
  the Valkyrie's modelled wins with the dog from 25% to 94%. So the older
  potions' weights were tripled (9 and 6, every ratio kept) to give it weight
  1 from Dlvl 15 -- one in 55 there, 25% -> 49%. A rarer thing needs the
  others multiplied, never a weight below 1. Quote a modelled win rate as the
  mean of several seeds (`run_batch(..., seed=)`): one seed of 600 lives
  wanders +-4 points, and a catalogue change reshuffles every draw.
- **The weapon ladder** (1.6) climbs in TWO-HANDED weapons (`slot` =
  `SL_TWOHAND`): the two-handed sword (6, Dlvl 16, appended after 1.6.2), the
  battle-axe (6, Dlvl 20) and the mattock (7, Dlvl 28);
  one-handed blades stop at 5 as NetHack's do (the silver saber, 5 from Dlvl
  24, doubles on V and i via `AF_SILVER`). A two-hander and a shield exclude
  each other -- `w` passes the two-hander over while a shield is worn, `W`
  the shield while one is wielded, and `blocked` turns the refusal into a
  reason. Measured: a one-handed 6 from Dlvl 14 alone took the Valkyrie with
  the dog from 53% to ~85%. Trading the shield costs little once `ARMOR_CAP`
  is reached, so `balance.py runs --twohand` (the player takes the shield off) is
  the number to watch: in the corrected model ~84% against ~59% for the game's
  own choosing. Adding the sword moved neither by more than the noise; a 7
  from Dlvl 24 instead would have put the trader at 88%.
- **Four classes wear per-game looks** (`!` `?` `=` `/`): a Fisher-Yates shuffle
  seeded off `world_seed` with its OWN xorshift (never `rn2`, which would shift
  the game's stream every time an item is named). The pools hold only the word
  ("ruby"; `obj_desc` adds the noun), must stay at least as long as their class
  and at most `SHUF_MAX`, and the per-class ordinal is counted, not hand-kept.
  A type is learned by use, by watching it work (a wand that hit something, a
  ring that acted — `ring_noticed`), or by a scroll of identify.
- `w`/`W` equip the **best** carried weapon/armour (highest
  `prop + ench - ero`). `P` **asks** which ring or amulet — choosing for the
  player would reveal an unknown ring's worth. The combat globals (`weapon_dmg`,
  `armor_def`, `ac`) are recomputed by `recompute_gear()` from the worn items;
  only the ring of protection adds to them. The other rings are pure effects,
  read through the resident `ring_fx` (`RF_*` bits, whose order IS the order of
  `O_RSLOWDIG..O_RTPORT`), recomputed with the worn set and never saved.
- **Artifacts** (1.6) are unique and never generated (`mindep` 255): Excalibur
  from the fountain, and five sacrifice gifts (`art_gift` in `item.c`: a pleased
  god on your own altar, from XL 3 with luck >= 0, one in 4, then 12, 20 --
  always one of your alignment or unaligned). `art_given` (resident, saved)
  names those this game holds; any new way to make one must set its bit. The
  wielded one's powers are `art_fx` (`AF_*`), recomputed like `ring_fx` and read
  by `monster_ai.c`: double damage to its quarry, Stormbringer's drain, drain
  resistance, Trollsbane stopping troll regeneration, Sting's glow. `w` ranks
  any artifact above every ordinary weapon -- it chooses for the player, and a
  bare number would let a +2 long sword shadow the god's gift for good. Acid
  never erodes one (`corrode_worn`).
- **Rules other code must keep asking** (1.6): poison resistance is
  `poison_res()` (the flesh's intrinsic OR the worn amulet versus poison) --
  a new poison source must ask it, not `INTR_POISON_RES` alone. Teleportitis
  is the ring's `RF_TPORT` OR the fey-flesh `INTR_TPORT`. No monster strikes
  a hero on a live Elbereth or on a scroll of scare monster lying underfoot:
  both chase paths read `ward`, worked out once per `monsters_turn`. That
  scroll's `ench` is its "picked up before" mark (dust on the next pickup).
  Stealth is `stealthy()` (`game.h`): the ring's `RF_STEALTH` OR the Rogue's
  birth `INTR_STEALTH` -- test the macro, never the ring bit alone.
  Elbereth (1.8): `E` writes with a finger or a wand (`do_engrave`), a wand's
  mark naming it; fire burns it for the visit (`EL_BURNED`, which `upkeep`
  never ticks), so a melee blow struck from any engraving rubs it out
  (`try_move`), as in NetHack 3.6 -- any new way to attack from the
  engraved square should do the same.
  Attributes have peaks (1.8, `at_peak[]`, saved, NetHack's AMAX): the class
  sets them, a gain past one carries it up (`at_gain`, `item_use.c`), and the
  cursed throne's drain lowers the attribute but never its peak -- which is
  what a potion of restore ability puts back. A new way to raise an
  attribute goes through `at_gain`; a new drain leaves the peak alone.
- **The 1.9 classes** (`classes.c`, appended so no saved `pclass` shifts): the
  Samurai is born Fast (`classes[].intr`), the Barbarian poison-resistant
  with a two-handed sword, the Healer carries the healing book, a wand of
  sleep and 200 gold, and the Priest sees every object's B/U/C -- `obj_desc`
  prints it for him whatever the known bit says, and `inv_add` sets the bit
  on what he takes, because the welds and the stack merging read it.
- **Class signatures** (1.6, `PC_*` in `game.h` name the `classes[]` rows): the
  Wizard regains Pw every 8 turns (`upkeep`) and force bolt is d(2,12); the
  Rogue is born stealthy and backstabs a sleeper for `rn2(xlvl)+1+xlvl/4` more
  (`hit_monster`); the Tourist's kit holds the **expensive camera**, a `(` with
  charges in `ench` like a wand's (charging reloads it, `obj_desc` prints
  them). Its flash sets `m_blind[]` 5..14: a blind monster ambles, cannot
  dodge, never breathes, and is out of the flood. `m_blind` is not saved and
  NOT cleared by `m_alive = 0`: `spawn_level_monsters` zeroes the array and
  the summon and wanderer sites zero their slot -- a new spawn site must too,
  or the newcomer inherits a dead monster's blindness.
- **Tools** (`(`, 1.6) are applied with `a` (`do_apply`, `item_use.c`): the
  skeleton key (70 + Dex in 100 per try, `door_unlock`), the pick-axe, the
  magic whistle, the blindfold and the unicorn horn; a slain dwarf leaves a
  pick-axe one time in three. Two of them borrow machinery instead of adding
  it. Digging is the rest loop's occupation: `resting == 2`, with `dig_left`
  turns of work on `dig_x/dig_y` in `rest_step`, so a monster in view stops it
  as it stops a rest and `mainentry.c` (resident) did not grow. A stop costs
  no work, and `a` at the same spot carries on ("You continue digging").
  The blindfold makes `upkeep` keep
  `st_blind` topped up while `blindfolded` is set, so every blind test in the
  game works unchanged; taking it off clears blindness only when no potion
  added to it (`st_blind <= 2`).
- **The pick-axe digs where it is pointed** (`apply_pick`): `>` down (five
  turns, the wand of digging's hole), a direction through rock or a wall
  (three). Refused, at no cost: a door, a shop's walls (`shop_in_room` covers
  the rect's walls), the map's outer ring (the generator never digs it, and
  nothing should) and Dlvl 50. Every dug cell becomes `#`, a wall's breach
  too -- never `.`, where `trap_type`'s position hash would hide a trap in one
  in 47 of them. **A level remembers its tunnels by cell**, unlike every other
  persistence here: `dug_pool` (`levelfov.c`) is a count and 3-byte records
  (level, `y*MAPW+x`) in Bank 5's free tail, re-opened by `dug_restore` at the
  end of `build_level` -- after gen, spawns, persistence, altar and fountain,
  so all of those see the level as it was born, and before `floor_restore`,
  because a stash may lie in a tunnel. Full (`DUG_MAX` 128), it forgets the
  oldest record of another floor; if every record is this floor's, the new
  hole opens but is not remembered. `fov_reset` empties it (new game, the
  attract demo), and it rides the level block of the save. **A dried fountain
  is the same kind of record** (`DUG_FLOOR`, 2026-10-10): `place_fountain`
  puts the `{` back on every visit, so until then a fountain that "dried up"
  was full again on the next one -- endless gold and healing. Its record
  reopens the cell as `.`, and the mark is bit 7 of the record's *level* byte,
  not of the cell index, so an older binary loading the save skips the record
  instead of writing 32 KB past `lvl[]`; the format and `SAVE_VER` are
  unchanged. Anything else the hero changes for good on a regenerated level
  belongs in this pool too -- and 1.8 put two more kinds there the same way,
  in bit 6 of the level byte (levels end at 54): `DUG_DOOR`, a found secret
  door reopened as `+`, and `DUG_MARK` (bits 6+7), which writes nothing and is
  found by `dug_marked`. A 1.7 binary masks only bit 7, so it reads both as a
  level above 54 and skips them.
- **A new floor class char must be taught to six places**, and nothing checks
  the list: `tile_for` (`level.c`), `do_pickup`'s accepted classes and
  `resolve_floor`'s BUC rule (`item.c`), `describe` and `lookable`
  (`nexthack.c`), the farlook switch (`farlook.c`), plus generation (`levelgen.c`'s loot blocks
  and `SHOPCLS`). Missing the pickup list makes the item visible, describable
  and impossible to take.
- `q`/`e`/`r`/`P`/`T` use `select_item()`: silent when exactly one entry
  matches, a letter menu for two or more -- of one type too (#19: two wands of
  striking, one empty, or a second ring like the first, were unreachable when
  the menu waited for two *different* types) -- and derives its
  prompt from the class (pseudo-classes: `'P'` rings + wearable amulets not
  already on, `'C'`
  wands for charging, `'T'` whatever is worn or wielded). These set `acted`/`turns` themselves, so a cancel or a
  no-op costs no turn.
- **Every teleport of the hero goes through `hero_teleport()`** (`nexthack.c`):
  the scroll, the trap, the spell and teleportitis. It is where teleport control
  asks; assigning `hero_x/y` from `level_random_floor` anywhere else would
  silently bypass the ring. `level_random_floor` only returns a square `tele_ok`
  accepts -- ground, no door, nobody there -- because a room rect is FOV metadata,
  not a promise: the cavern/crypt/temple rects hold rock and walls, and the mines
  scatter pillars. Twelve refused rolls fall back to a map sweep.
- **Items the hero owned never just vanish.** `floor_drop` says no to a taken
  cell or a full floor (`MAXFLOOR` 8); for a thrown weapon or a nymph's loot use
  `floor_place` (`item_floor_place` from `item_use.c`), which tries the
  neighbours and evicts the oldest corpse before failing -- and callers keep the
  object until it is down. Corpses and death loot stay on plain `floor_drop`.
- **A cursed piece the hero has on stays on.** Every way an item comes off or
  leaves the pack is a way out of a curse: `T` (take off, 1.9) and `d` (drop
  and sell) ask `cursed_on()` in `item.c`, and `t` checks the weld itself in
  `item_use.c`. A new verb that parts with an item must ask too. From BUC's
  arrival until the 1.4 bug hunt, `d` did not, and every curse was a formality.
  `P` fills a free place only -- two ring hands, one neck -- so a swap is `T`
  then `P`, and the cursed one cannot be the one that makes way.
- **Any 128K list goes through `list_row()`.** The ULA has 23 rows under a
  header and the pack holds 26: the inventory and drop lists used to stop at
  the 23rd item, and an item menu with no stop drew below row 23, into the
  printer buffer and the system variables. `list_row` pages with `--More--`.
  The Next's one-per-row menus run past the status bar into rows 24-31, which
  nothing repaints, so they end with `menu_tail_clear()`.

### Monster AI (`monster.c`)
- Monster types are a table (`montypes[]`: char, hp, damage, xp, min depth, tile,
  `corr`, `atk`, `spd`, `freq`, `maxdep`, name); `spawn_level_monsters()` draws from the depth-appropriate pool.
  HP/damage scale with depth. A `corr` (corrosive) monster — the acid blob — calls
  `corrode_worn()` (in `item.c`) to rust the hero's armour when it bites and the
  wielded weapon when struck; erosion raises `obj_t.ero`, capped at 3.
- **Speed** (1.9): `MonType.spd`, banked per slot in `m_mv[]` each turn
  (`monsters_turn`, `monster_ai.c`): a monster acts once per `NORMAL_SPEED`
  (12) it can pay, so the old body is now `monsters_phase()`, run at most
  twice for the slots in `act` -- **every per-monster loop in it must test
  `ACTS(i)`, on both target paths**, or a monster acts on points it does not
  have. A Fast hero (`INTR_FAST`, from a wand of speed monster zapped at
  yourself with `.`) denies them the points one turn in four; the wand at a
  monster sets its `m_haste` bit (+6), cleared with `m_blind` at every spawn
  site. NetHack's own speeds made the game far easier (balance.py: Valkyrie
  96%, Tourist 79%), mostly through the pet `'d'` at 16 and the slow dragon,
  so the table keeps the flavour at half the spread -- the reasoning and the
  numbers are in the comment above `montypes[]`. `balance.py`'s `fight`
  banks points the same way for the monster and the dog.
- **Weights and windows** (1.9): `pick_mon` draws a depth's eligible types in
  proportion to `MonType.freq`, and a type spawns only from `mindepth` to
  `maxdep` (255 = for ever) -- still one `rn2`, and `balance.py`'s `pool()`
  repeats each type `freq` times so the model draws exactly what the game
  does. The old roster weighs 2 with no limit; the ten 1.9 types (unicorn,
  rothe, quantum mechanic, rust monster, ogre, giant spider, umber hulk,
  black pudding, hill giant, lich) weigh 1 and spawn for ten levels from
  their first. **Add a monster that way, and measure it**: every bite grows
  by `eff_depth()/4`, so a sturdy type that stays eligible for ever
  lengthens every deeper fight -- added plainly the ten took the Valkyrie to
  4% and the Tourist to 0, even when each was tuned to cost no more than its
  strongest peer at its own depth (`balance.py duel`). The fuzzer checks the
  windows (`mon-window`, the level's own spawns only: summons and wanderers
  are not tracked). New attacks: `ATK_TELE` (the mechanic: `hero_teleport`,
  so teleport control asks) and `ATK_CONF` (the hulk). A kill's tool is
  `kill_tool(x, y, mch)` (`item_use.c`): the dwarf's pick-axe, one in three,
  and the unicorn's horn. Corpses: the spider poisons, the pudding burns, the
  mechanic toggles `INTR_FAST`. **Tile space is nearly gone**: the Next's
  tile defs end 64 B under `inv[]` (two tiles), the 128K's UDGs 24 B under
  the mirrored annex (three) -- the next monsters need room made first.
- **Genocide** is a resident 8-byte mask by monster char (`mon_geno`, saved with
  the kill masks), and it has two filters in `monster_spawn.c` -- NOT inside the
  resident `pick_mon`, where three expansions of the mask test cost the Next
  150 B of a resident half that had 285. A **transient** spawn (wanderer) goes
  through `pick_living()`, which re-rolls a genocided draw. The **level's own**
  spawns must not: they draw exactly as with no genocide, and
  `strike_genocided()` then empties the genocided slots. A re-roll shifts the
  stream, so through 1.8.0 a revisited level grew other monsters in other places
  and an old `mon_dead` bit struck the wrong one off (#15). A new spawn site
  takes the filter that matches what it is.
- Pathfinding is a **per-turn BFS "Dijkstra map"** from the hero (`compute_dist_map`)
  over walkable cells; each monster steps to the lowest-distance neighbour. One search
  serves all monsters. The frontier queue `bfsq` is a **ring** of 256 (`BFSQ_SIZE`,
  uint8_t slot) -- a flood only ever holds about two distance layers, at most 67
  cells on any shipped map, where the old linear 696-entry queue counted every cell
  ever enqueued and stopped: 101-578 walkable cells of the maze, the cavern and the
  Big Room never got a distance. The flood takes a mask of the monsters that will
  READ it this turn (`who`), marks their cells `TARGET` and **stops when the last is
  labelled** -- every closer layer is complete by then, so the moves are exactly a
  whole-map field's; no reader, no flood. The eight neighbours are unrolled
  (`FLOOD_TRY`) -- the dx/dy loops kept their ints in IX memory. Measured on the
  128K Big Room, a wall-boxed chaser 10 squares off: 0.9 s of flood before,
  ~0.25 s after (FRAMES-timed under ZEsarUX); 20 squares off, 0.5 s where the old
  flood never reached it at all. On the 128K the loop itself is hand-written Z80
  (`flood_run`, inline asm in `monster_ai.c` so it stays in that module's bank) and
  stops at `MAXDIST` (`fl_max`; none when the dog alone reads it); it mirrors
  `FLOOD_TRY` test for test, so **a change to the flood's rules must be made in
  both**.
- **The 128K's dog follows its nose, then the field** (`pet_heel_greedy`,
  `pet_route`). With no enemy near it heels by line of sight and floods only when a
  wall boxes it, and from then on it keeps to the field until it is back at heel
  by the path (2026-10-25). Handed back to its nose after every routed step, the
  two fought and the dog was lost for good: a user's Samurai left a room by one
  door, and the dog ran along the inside wall, left by the other door and parked
  against the wall nearest the hero, 54 steps round and past `MAXDIST`. Proven in
  ZEsarUX on both halves -- a walk out of a room that left the old dog bouncing
  between two cells 21 steps back, and a dog 39 steps off that never moved -- and
  timed in z88dk-ticks: a flood to a dog past `MAXDIST` costs 35-47 ms a turn
  while it catches up, where the old one cost 23 ms every turn, for good. The
  Next never had the bug: its dog reads the whole field every turn.
- **Kill persistence names spawn identities, not slot numbers.** `m_track` (bit i:
  slot i holds the monster the level spawned there) gates every `mon_dead` write:
  keeper, pet and followers are appended above the spawns, and a wanderer or summon
  that moves into a dead slot clears its bit. A new spawn or kill site must keep it.
  A stair follower that was one of its level's own spawns is struck off there
  (`mon_dead`) once it is placed beside you (`place_follower`, after 1.8.0):
  its home regenerates it, so a follower killed abroad used to be back on the
  next visit, its XP to be had again (#13). The Amulet's high priest never
  follows.

### Turn loop & input (`nexthack.c main`)
- Loop: read key (`getkey_rpt`) → act → if the action took a turn, `upkeep()`
  (hunger/regen) then `monsters_turn()` → recompute FOV → redraw → handle death.
- Held keys pace via **typematic repeat** in `getkey_rpt` (`platform.c`): a tap is
  exactly one step, a hold repeats after a first beat then a steady one. Do NOT try
  to throttle with `in_pause` — it returns immediately while any key is down. The
  128K times by the FRAMES sysvar, and counts each beat from the moment the last
  step was **handed out** (`given`), so the turn's own work spends the beat instead
  of adding to it: a held key steps every max(turn, beat) -- 220 ms, then 100 ms.
  Counted from the redraw's end (through 1.6.0), the 128K walked at turn + beat,
  140-180 ms a step against the Next's ~107 (MAME). The bare `.nex` runs
  with interrupts off (FRAMES dead), so there the bounded poll loop itself is the
  ~20 ms clock (`RPT_GUARD`) and the beat still follows the turn.
- Movement: cursor keys **and** vi-keys (`hjkl`+`yubn`). Commands are NetHack-style
  single letters (`,` `i` `w` `W` `P` `T` `q` `e` `r` `S`, `>`/`<`/Enter for stairs). Uppercase
  is **not** folded to lowercase (so `w` wield vs `W` wear are distinct).

### Sound (`sfx.c`)
- Beeper effects via z88dk's `bit_beepfx`. The effects are cycle-timed for 3.5 MHz, so
  `sfx_*` drops the CPU to 3.5 MHz for the effect and restores 28 MHz after.

## Memory budget — the constraint to respect (CODE-BANKED architecture)
The game **broke the 64 KB ceiling by code-banking**. Layout:
- **Resident** (`0x8000-0xBFF0`, one 16 KB bank): hot code + **all** data/BSS + the
  512 B stack (`REGISTER_SP=0xBFF0`). This is the tight half.
- **Banked** (`0xC000-0xFFFF`, `CLIB_BANKING_SEGMENT=3`): cold code in six pages on
  the Next — `PAGE_20/22/26/28/30/44_CODE` (banks 10/11/13/14/15/22; 22 opened in
  1.8, the first past the Layer 2 images) — and six on the 128K
  (`BANK_0/1/3/4/6/7`), mapped in on demand by the z88dk `__banked` trampoline. They
  are **not** interchangeable free space: `banks.json` says which module goes where,
  and some are full while others are half empty. **Ask `bankmap.py`, never guess.**
- **Bank 5** (`0x4000-0x7FFF`, always mapped): tilemap + tile defs, and its free tail
  holds the BFS scratch `dist[]`+`bfsq[]` (`0x7400-0x7C90`, data-banked out of
  resident) and, above it, the pick-axe's `dug_pool` (`0x7C90-0x7E11`, 1.6) --
  persistent and saved, unlike the scratch below it. `0x7E11-0x8000` (495 B) is
  free on the Next; on the 128K the renderer's full path took `MON_MAP`
  (`0x7E18`, 210 B) and `DM_CTAB` (`0x7F80`, 128 B), leaving ~150 B. The 128K
  also keeps **the font** at `0x5D00-0x5FFF` (`font_ram`, 1.7): the tape
  loader's BASIC workspace while it loads, nobody's once the game runs.

**The resident half is the constraint.** Everything resident (code+data+BSS) must end
below `0xBDF0` — `REGISTER_SP` is `0xBFF0` and the stack wants the 512 B under it — or
the stack corrupts and the machine resets to BASIC.

**Do not trust a number written here: run `python tools/bankmap.py`.** It reads both
`.map` files and prints the resident half, every bank's free tail and the Bank-5
tenant map. Any figure in this document is a snapshot of the commit that wrote it, and
this is the one place where a stale number costs a debugging session. Its headroom
figure is measured to `$BDF0` and reaches 0 exactly where the stack reserve begins,
so it is the number you can spend directly. As of 2026-09-27 the Next sat at
`__BSS_END=$BCFE` — 242 B — and the 128K at `$B4E9`, ~2.3 KB; dropping the
unused terminal runtime (1.8, `-startup=31`) took them to `$AE5C` (3988 B) and
`$A798` (5720 B).

**Adding a feature:**
- **New code → make it banked** (there is room, though not in every bank): declare the
  module in `banks.json` and mark its entry points `__banked`. `python
  tools/bankpack.py plan <target>` picks the bank for you — that is what it is for.
  Cold/per-turn code banks freely (the trampoline cost is negligible off the per-cell
  path).
- **New resident DATA is still the scarce resource.** Banked code's `static` data —
  **and its string/const literals (resident rodata)** — stay resident, so data/text-heavy
  features eat the few hundred bytes fast (the shops' message strings did). Levers when it overflows:
  (a) **const-bank read-once tables** — `gfx[]` (~3 KB, read only by `load_gfx_tiles` at
  startup) lives in `platform_init.c`, whose `const` `banks.json` puts in `PAGE_20_CODE`,
  so it sits next to its reader (which runs with that page mapped); (b) data-bank scratch
  arrays into Bank 5's free tail (like `dist`/`bfsq`); (c) trim strings.
- **`--max-allocs-per-node200000` is load-bearing for resident code size** (the SDCC
  allocator's thoroughness shrinks code); don't lower it.

**Partitioning rules (how the split is done — see `nextzxos-banking-findings` memory):**
1. Split a mixed module into wholly-hot/wholly-cold **files** — the assignment is
   per-module (one `banks.json` entry per `.c`), and an in-file `#pragma codeseg` does
   NOT partition by position either (it scrambles sections). A file is the unit.
2. A module containing `main()` → bank the module, move only `main()` to a tiny resident
   file (`mainentry.c`); the CRT jumps straight to main, so it can't be banked.
3. Keep the per-cell/per-move **leaves** resident (`platform.c` draw primitives,
   `level.c` terrain/walkable/tile_for, `monster.c` monster_at/mon_find/…) so banked
   callers reach them by direct calls; banked entry points are `__banked`, intra-page
   static helpers stay plain. (Read-once tables like `gfx[]` are *not* leaves — bank them.)
4. Data shared across a split is defined in one file, `extern` in the other (DATA is
   resident regardless of which file's code is banked).
5. Resident modules: `mainentry.c`, `platform.c`, `level.c`, `monster.c`, `rng.c`
   (they carry no `code`/`const` key). Everything else is banked — **`banks.json` is
   the authoritative list**, per target, with the reason for each placement; read it
   rather than a list here, which goes stale every time a bank fills up.
