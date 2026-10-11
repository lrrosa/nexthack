---
name: zrcp-verify
description: Verify NextHack behaviour by driving it inside ZEsarUX over ZRCP (read/poke memory, inject keys, decode the screen) on the Next and/or the 128K, or headless inside MAME (mame.ps1) as a second emulator, where fuzz/fuzz.ps1 also fuzzes it for invariant and memory-guard violations. Use whenever a change needs proving in the emulator rather than by reading code — new commands, tiles, AI/movement, traps, save/restore, rendering, the 128K tape loader, timing — and before calling any feature done. There are no automated tests in this repo; this IS the test harness.
---

# Verifying NextHack in the emulator (ZRCP)

ZEsarUX exposes **ZRCP**, a TCP debug protocol on `:10000` — registers, MMU,
memory read/write, key injection. That is enough to play the game
programmatically and assert on real state, which is how every feature in this
project gets proven. The agent can do the whole loop itself.

**Never hand-roll the plumbing.** Dot-source the harness:

```powershell
. 'G:\nethackNext\port\.claude\skills\zrcp-verify\zrcp.ps1'
Start-Emu next            # or: Start-Emu zx128
Start-NewGame             # dismisses the title, picks class 'a'
```

Write the script to a scratchpad `.ps1` and run it with `&`. Multi-line works
that way; one-liners do not scale.

## The harness API

| Call | Does |
|---|---|
| `Start-Emu next\|zx128` | kills any running emulator, launches with the right flags, connects, stashes `nexthack.sav` |
| `Start-NewGame [-Class a]` | title → class pick → playable |
| `Sym hero_x` | address from the CURRENT `.map` (never hard-code: every build shifts them) |
| `Get-Byte/Get-Word/Get-Bytes` | read (handles the retry that transient failures need) |
| `Set-Bytes/Set-Word/Set-Hero` | poke state |
| `Send-Key 108` | inject one ASCII key (`108`=`l`, `62`=`>`, `59`=`;`, `68`=`D`) |
| `Get-MsgLine` | row 0 as **text**, target-aware (see below) |
| `Get-TileCell x y` | Next: `@(tile, attr)` at a map cell |
| `Get-ShadowTile x y` | 128K: what `draw_map` last painted there |
| `Invoke-Descend` | teleport onto `>` and take it; returns the new `dlvl` |
| `Set-Tank [200]` | survive a long test |
| `Save-EmuScreenshot path.png` | grabs the window even when occluded |

## Procedure

1. **Build first**, and do not edit sources while a build runs — that yields a
   mixed `.o` set (symptom: a new symbol missing from the `.map` at unchanged
   resident size). After any edit-during-build, `grep` the map, or `-Clean`.
2. `Start-Emu <target>` — it relaunches; see the trap list.
3. Drive the feature and **assert on state**, not on vibes: positions, `turns`,
   `dlvl`, tile ids, message text.
4. **Verify on BOTH targets** when the change touches rendering, movement or
   the AI. The 128K has its own `draw_map` (3-tier, its full path in Z80) and its
   own greedy chase and Z80 flood —
   a fix in the shared path can still be missing from the 128K one.
5. Report what the reads actually said. If a check did not run, say so.

## Traps that have cost real time here

- **`set-machine` KILLS ZEsarUX v13** — the process dies, not just the
  connection. To switch targets, relaunch (`Start-Emu` does). Always check the
  process is alive before connecting; it also dies randomly between runs.
- **A boot consumes and deletes `nexthack.sav`.** Stash it at the START of the
  session (`Start-Emu` does this), never inside a test that may not run.
- **Addresses shift on every rebuild.** Always `Sym`; a stale address reads
  garbage that looks exactly like a game-state bug.
- **`read-memory` takes DECIMAL and prints HEX; `write-memory` takes decimal
  for BOTH the address and the values.** A value read as `46` must be written
  as `70`.
- **Do not interpolate arithmetic into a command string** — `"read-memory
  ($a+2) 4"` is sent literally and returns `0xF3`. Pre-compute into a variable.
- **A monster poked INTO A WALL cannot be bump-attacked** (`try_move` rejects
  the wall first) — this produced two false-negative kill tests.
- **A poked terrain cell on the 128K needs `map_flush=1`**, or the 3-tier
  `draw_map` never repaints it. Also scan the `lvl` row for a real `.` cell
  instead of guessing offsets.
- **Poking a monster into a slot while shrinking `mcount` orphans the PET**,
  which turns hostile and wanders into your test.
- **Timing is not measurable while polling**: `read-memory` pauses the CPU, so
  a polled turn reads ~4x its real duration. To time something, send one key,
  `Start-Sleep` with ZERO ZRCP traffic, then read once and bisect.
- **The render target differs**: Next = hardware tilemap at `0x6000`
  (2 B/cell); 128K = ULA bitmap `0x4000` + attrs `0x5800`, so text must be
  matched against the ROM font — `Get-MsgLine` already does both.
- The Next's title/victory are **Layer 2**, invisible to ZRCP. Verify those
  with `Save-EmuScreenshot`.

## Worked example

```powershell
. 'G:\nethackNext\port\.claude\skills\zrcp-verify\zrcp.ps1'
Start-Emu zx128
Start-NewGame
Set-Tank
# the pet must swap past a peaceful gnome instead of being corked behind it
$pet = Get-Byte (Sym pet_idx)
Set-Bytes (Sym m_type) @(71)            # slot 0 = 'G'
Set-Bytes (Sym m_peace) @(1)
Set-Bytes (Sym m_alive) @(1)
Send-Key 108
"dog=$(Get-Byte ((Sym m_x) + $pet))  gnome=$(Get-Byte (Sym m_x))"
Disconnect-Zrcp
```

Staging richer scenes: poke stats, read a **scroll of magic mapping**
(`Set-Bytes (Sym inv_count) @(1)` with `otyp 13` in `inv[]`, then `r`) to
reveal the level, and pull monsters into frame before `Save-EmuScreenshot`.
`inv[]` is Bank-5 resident: `0x5800` (Next) / `0x7360` (128K; it was `0x6800`
before 1.6 -- an old script poking there now writes the UDG bitmap).

## Second emulator: MAME (`mame.ps1`)

MAME 0.289 (`F:\jogos\emuladores\mame`, or `$env:NEXTHACK_MAME`) runs both
targets. It has no debug socket, so a MAME test is a **script of steps
written up front** and played by `mame.lua` (its `-autoboot_script`); the
step vocabulary is at the top of `mame.lua`. Reach for it when:

- the **128K tape** matters: it loads the real `.tap` through the real 128 ROM
  (menu -> Tape Loader -> LD-BYTES -> every bank block -> boot stub), which
  ZEsarUX's autoload shortcuts;
- **timing** matters: reads happen between frames, outside emulated time, so
  unlike ZRCP they do not stretch what they measure;
- a second opinion on hardware fidelity (MAME emulates 128K contention).

It runs **headless** by default (`-video none`, host keyboard/mouse/joystick
off) with `-nothrottle`: the Next test takes ~3 s wall, the 128K ~30 s (the
tape is ~616 s of emulated time). Nothing is written to the MAME folder: cfg,
nvram, the SD's diff and the snapshots go under `-OutDir`
(default `%TEMP%\nexthack-mame\<Tag>`, with `log.txt` and `mame.out`).

```powershell
. 'G:\nethackNext\port\.claude\skills\zrcp-verify\mame.ps1'
$r = Invoke-Mame next -Tag pickup -Steps ((Get-MameBoot next) + (Get-MameNewGame) + @(
         'peek @turns 2', 'poke @php 200', 'key ,', 'wait 1', 'msg', 'peek @turns 2', 'snap after'))
Get-MameMsg $r                      # 'Nothing here to pick up.'
Get-MamePeek $r turns               # every read of that label, oldest first
```

| Call | Does |
|---|---|
| `Invoke-Mame next\|zx128 -Steps @(...)` | runs the script, returns `.Log` + `.Dir` (snapshots); `-Tag`, `-OutDir`, `-Visible`, `-Throttle`, `-MaxSeconds`, `-Sd`, `-FreshSd`, `-ExtraArgs` |
| `Get-MameBoot next\|zx128` | steps up to the title (the 128K's includes the whole tape load) |
| `Get-MameNewGame [-Class a] [-Seed n]` | title -> class pick -> playable; `-Seed` pins the world (below) |
| `Get-MamePeek $r label` / `Get-MameMsg $r` | parse the log |

In steps, addresses are `@sym`, `@sym+N` (from the CURRENT `.map`), `0xHEX` or
decimal; poke values are decimal. `peek <addr> [n] [label]`, `msg` decodes row
0 on both targets, `snap <name>` saves a PNG, `time` logs emulated time, and
`copy <src> <dst> <n>` copies bytes at run time -- a fixed script can still act
on what the game decided (`copy @dn_x @hero_x 1`: onto the down stairs).

**Timing and profiling** (how the 128K's walk was measured against the Next's,
and made to match it). `watch`, `hold`/`release` and `sampleon`/`sampleoff`
log what `mameprof.py` turns into a per-function profile of the turn's work
(idle key polling left out) and the hero's step timing:

```powershell
$r = Invoke-Mame zx128 -Tag walk -Steps ((Get-MameBoot zx128) + (Get-MameNewGame -Seed 0x1234) + @(
         'watch @hero_x 2', 'watch @hero_y 2', 'watch @turns 2', 'watch @prev_hx 1',
         'sampleon 2000', 'hold l 100', 'release', 'wait 0.7', 'sampleoff room'))
python .claude/skills/zrcp-verify/mameprof.py "$($r.Dir)\log.txt" zx128
```
- The sampler reads PC on `emu.wait` timers, at exact emulated instants -- unlike
  ZRCP, sampling does not stretch the time it measures. On the 128K a banked PC
  resolves by the paged bank (BANKM); on the Next it cannot tell the banks apart.
- `-Seed` matters for any A/B: the title seeds the world from how long the key
  took, so two builds whose tapes load in different times play different worlds.
  It pokes `world_seed` and `rng` while the class picker waits, before any level
  exists. Compare an old build by copying its `.tap`/`.nex` and `.map` to a folder
  and pointing `$script:MamePort` at it (or build it in a `git worktree`).
- A **hold** measures the walk; for comparing two builds' *state*, use taps with
  generous waits (`key l`, `wait 1.0`): a tap that lands during a long redraw is
  lost, and the faster build then takes more steps (caught here: the old 128K
  dropped a tap during its 0.6 s scroll).
- `move -> drawn` needs `watch @prev_hx 1` (128K): the time from the hero's move
  to the frame that drew it, i.e. the turn's own cost, beat excluded.

**What each target can and cannot do in MAME:**
- **128K (`spec128`)**: everything but saving -- MAME's 128K has no DivMMC, so
  the game correctly finds no esxDOS.
- **Next (`specnext_ks2`, `.nex` via `-dump`)**: game logic, tilemap, Layer 2
  and keys are real, but with no SD card the ROM at `0x0000` is the TBBlue boot
  loader, so the font the game copies from `0x3C00` is **garbage on screen** --
  judge text with `msg` (a tile id IS its ASCII code), not with snapshots -- and
  there is no esxDOS, so no saves.
- **Next `-Sd`** (boot NextZXOS off `roms\specnext_sd\sys2411.chd`, then mount
  the `.nex`): **does not boot in MAME 0.289**, so saves cannot be tested there.
  `sys2411.chd` is System/Next 24.11 (`cspect-next-1gb.img` from
  `sn-emulator-24.11.zip`, CHD SHA1 `fe6e1078...`; not the software list's
  `947b7598...`). The boot ROM loads `TBBLUE.FW`, whose configuration screen
  then stops on "Error opening 'menu.ini/.def'!" although
  `/machines/next/menu.def` is on the card: it opens files in the root but not
  in a subdirectory. Same on ks1/ks2/ks3/tbblue. ZEsarUX boots the same image
  through the same firmware, so the fault is MAME's SD/DMA emulation (its SD
  card is SDSC at this size, and `spi_sdcard.cpp` carries TODOs on multi-block
  reads). The 2020 CSpect 2 GB image (`cspect-2020-2gb.chd`) is worse: the boot
  ROM freezes at PC `0192`. Prove saves on ZEsarUX.

**MAME traps:**
- **Two MAME runs cannot share one `.tap`/`.nex`**: MAME opens the image
  read-write and the second instance dies on "Permission denied" (no log at
  all). Running builds side by side, give each its own copy.
- **Never type during the tape load**: SPACE is BREAK to LD-BYTES and silently
  kills it. `Get-MameBoot zx128` waits for `PC >= 0x8000` before handing over.
- **The Next driver's natural keyboard has no SYMBOL SHIFT**: MAME silently
  drops `,` `<` `>` `;` `?` `:` `\` and friends. `mame.lua` presses a lone
  symbol as its chord on the matrix fields (SYMBOL SHIFT + key, ~100 ms), on
  both targets. Typing a symbol inside a longer `key` string is still dropped.
- **Keep it headless.** A visible MAME window takes the focus when it opens,
  and whatever the user types then lands in the emulated keyboard: identical
  scripts produced different worlds until the host input was cut off.
  Headless, identical scripts reproduce the same world run after run -- but a
  different script (one extra frame before the key press) seeds a different
  one, so never hard-code positions: read `hero_x`, or poke.
- A script that stalls (a `waitpc` never met) is killed by `-MaxSeconds` of
  emulated time; the log then lacks `exit` and `Invoke-Mame` warns.

## Fuzzing (`fuzz/`)

`fuzz/fuzz.ps1` boots the real `.tap`/`.nex` headless in MAME with its
debugger on (`-debug -debugger none`), plays weighted random keys and checks
the game's invariants at every key wait. It is what found the 2026-10-08..10
bugs no scripted test had: a dried fountain refilling on a revisit, an altar
or fountain appearing where a pile was taken, and 128K ghost monsters after
telepathy ended. Run it after touching generation, persistence, the renderer
or a bank -- one seed per target at least.

```powershell
& .claude\skills\zrcp-verify\fuzz\fuzz.ps1 zx128 -Seed 7 -Frames 30000   # play
& .claude\skills\zrcp-verify\fuzz\fuzz.ps1 next -Scan -Seed 7 -Stairs 40 # visit levels
python .claude\skills\zrcp-verify\fuzz\cover.py nexthack128.map "$env:TEMP\nexthack-fuzz\*\log.txt.cover"
```

- `-Seed` pins the world, the dice and every key: the same seed, frames and
  build replay the same run, so a report is reproduced by rerunning it.
  Almost always: of 19 identical Next runs (one image, one config) 18
  matched trip for trip and one diverged, cause unknown. A report that does
  not come back on the first rerun deserves a second before it is dismissed.
- `-Frames` counts play after the class pick (50 a second). With the
  debugger on, the Next runs ~3x real time and the 128K ~14x (its ~616 s
  tape load included): 6000 frames take about a minute on either, 60000
  about seven on the Next. Parallel runs are fine; each `-Tag` gets its own
  copy of the image.
- `-Class a`, `-Stairs 70` (take the stairs every n actions; a scan visits
  more levels at 40), `-Dump` (also `$env:FUZZ_DUMP=1`: the last 40 keys and
  the map at each kind's first violation), `-OutDir` (default
  `%TEMP%\nexthack-fuzz`), `-Tag` (default `fz-<target>-<seed>[-scan]`).
- `-PortDir` is the tree whose build is fuzzed: the `.map`, `src/` tables and
  `tools/bankmap.py` are read from it (`fuzzcfg.py`, rerun every time). The
  default is this repo -- a worktree has no build, so point it at the
  checkout you built.

**What it checks.** At every wait of the MAIN loop's `getkey_rpt`: `dlvl`
1..54; the hero inside the map and on walkable ground; every monster in
bounds, not in rock, not stacked, not on the hero; `mcount <= MAXMON`; the
pack's count and `otyp`s; `php <= pmaxhp`; the pet slot alive and a dog;
`dug_pool`'s count; and a **renderer oracle** that recomputes every viewport
cell the way a forced full redraw would, against `VIEW_SHADOW` and the ULA
screen (128K) or the tilemap (Next). On each level's first check: `>`, `<`,
the mine entrance (Dlvl 2) and the Amulet's cell (Dlvl 50) reachable on foot,
one `<`, one `>` except at the two bottoms, the mine hole only on Dlvl 2.
Throughout: **write watchpoints** on memory the game must never write --
resident code, the code banks, Bank 5's gaps between tenants (from
`bankmap.py`), the free space between `__BSS_END` and the stack reserve -- and
on the 128K a `0x7FFD` write with bit 3 (shadow screen) or bit 5 (paging
lock); a **control** watchpoint that must fire (FRAMES on the 128K, a `Y` on
the Next's message line) proves they are armed. A 1 kHz sampler logs the
lowest SP and `tempsp` (the trampoline's stack) and the PCs for `cover.py`;
30 s without a key wait is a `stall`, 100 frames in ROM `in-rom`. To keep the
run going it tops up HP and food, pokes a gift into the pack every 25 actions
(wands, scrolls, tools, artifacts) and the hero onto the stairs every
`-Stairs` actions.

**`-Scan`** visits random levels of random worlds. A one-shot write
watchpoint on `build_level`'s `el_life = 0` (after `go_down`/`go_up` moved
`dlvl`, before `gen_level` reads it) pokes `dlvl` and `world_seed` and clears
the last world's stashes, masks, open doors and tunnels from INSIDE
`build_level`. Never before the key: a misread key would then play turns on a
new depth over the old floor. Each trip's action prints a serial to the
debugger console, which is how `scan trips N taken M` knows a trip happened.
A monster on the `>` (a sleeper never leaves) makes it climb `<` instead,
logged as `A ... blocked`; before that, one such sleeper held a 128K scan on
one level for 11000 of its 12500 frames. On a level whose altar or fountain spot holds a generated pile it runs the
**grow test**: take the pile, revisit the same level, the spot must still be
`.`. It mirrors `place_altar`/`place_fountain`'s hashes and `eff_depth`'s
mines rule; `tile_for` and the oracles mirror `level.c` and `draw_map`.
Change those and update `fuzz.lua`, or it reports phantoms or goes blind.

**Reading `<OutDir>\<Tag>\log.txt`** (`fuzz.ps1` prints it, `A` stairs lines
left out, and ends with a verdict line):
- `VIOLATION <kind> @frame F dlvl D turns T: <detail>` -- an invariant broke.
  The first 4 of a kind are logged, all are counted (`count <kind> N` after
  the summary); the first also saves `vNN_<kind>.png` (the Next's font is
  garbage there, see above).
- `HIT @frame F dlvl D: WP <guard> a=<addr> d=<data> pc=<pc> sp=<sp> bk=<bank>`
  -- a guard watchpoint: the game wrote where it never may. `bk` is BANKM on
  the 128K; `pc` + the `.map` name the writer.
- `SUMMARY seed ... actions ... checks ... deaths ...`, then: stairs keys
  lost, levels walk-checked, scan trips/taken, grow tests/passed, cursor
  skips; the levels visited; `min SP` against the reserve floor and `min
  tempsp` against the main stack's top; `count` lines; `WATCH Nx WP ...`
  per watchpoint that fired.
- **A clean run** has a SUMMARY, no `VIOLATION` or `HIT`, and a `WATCH ... WP
  control` line. No SUMMARY means MAME stopped early (`mame.out`); no control
  line means the guards proved nothing. `trip not taken` and `grow test
  abandoned` are noise (a key misread, a teleport), not findings.
- `log.txt.cover` feeds `cover.py`: the game's public functions no sample
  ever landed in. On the 128K a banked PC carries its bank; the Next's
  sampler cannot tell its banks apart, so a banked function there is listed
  only if no bank had a sample at its offset.

**Fuzzer traps:**
- **MAME's debugger reads numbers as hex, and a bare one that starts with a
  letter as a register**: `C`, `D`, `AF25` are registers, `28` is `0x28`.
  `0x`-prefix every number handed to it (`hx()` in `fuzz.lua`).
- **`getkey_rpt` is shared by the main loop and `cursor_pick`** (farlook,
  teleport control), which waits mid-turn, before the redraw. The PC cannot
  tell them apart; a return address into `cursor_pick` among the top stack
  words can, and no oracle runs there (`cursor skips`).
- **Two MAME instances cannot share one `.tap`** (see MAME traps): `fuzz.ps1`
  copies the image into each run's folder.
- **`pet_idx = -1` with `have_pet` set is legal**: `place_pet` lets the dog
  sit a level out when the arrival is packed or `MAXMON` is reached. It is
  not a lost pet.
- **A chord must not overlap the last key**, still held by MAME's natural
  keyboard: the game would see neither. A scan trip waits 8 quiet frames.
