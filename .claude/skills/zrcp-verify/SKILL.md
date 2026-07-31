---
name: zrcp-verify
description: Verify NextHack behaviour by driving it inside ZEsarUX over ZRCP (read/poke memory, inject keys, decode the screen) on the Next and/or the 128K. Use whenever a change needs proving in the emulator rather than by reading code — new commands, tiles, AI/movement, traps, save/restore, rendering — and before calling any feature done. There are no automated tests in this repo; this IS the test harness.
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
`inv[]` is Bank-5 resident: `0x5800` (Next) / `0x6800` (128K).
