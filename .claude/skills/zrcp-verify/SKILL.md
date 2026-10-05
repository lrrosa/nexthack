---
name: zrcp-verify
description: Verify NextHack behaviour by driving it inside ZEsarUX over ZRCP (read/poke memory, inject keys, decode the screen) on the Next and/or the 128K, or headless inside MAME (mame.ps1) as a second emulator. Use whenever a change needs proving in the emulator rather than by reading code — new commands, tiles, AI/movement, traps, save/restore, rendering, the 128K tape loader, timing — and before calling any feature done. There are no automated tests in this repo; this IS the test harness.
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
   the AI. The 128K has its own `draw_map` (3-tier) and its own greedy chase —
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
| `Get-MameNewGame [-Class a]` | title -> class pick -> playable |
| `Get-MamePeek $r label` / `Get-MameMsg $r` | parse the log |

In steps, addresses are `@sym`, `@sym+N` (from the CURRENT `.map`), `0xHEX` or
decimal; poke values are decimal. `peek <addr> [n] [label]`, `msg` decodes row
0 on both targets, `snap <name>` saves a PNG, `time` logs emulated time.

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
