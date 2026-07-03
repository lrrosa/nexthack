# Changelog

All notable changes to NextHack are documented here.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and the project follows [Semantic Versioning](https://semver.org/spec/v2.0.0.html).
Every release ships two binaries — `nexthack.nex` (ZX Spectrum Next) and
`nexthack128.tap` (ZX Spectrum 128K) — on the
[Releases](https://github.com/lrrosa/nexthack/releases) page.

## [1.5.1] — 2026-07-03

A hotfix + polish pass over 1.5.0. Saved games from 1.5.0 still load.

### Fixed
- **ZX Spectrum 128K: entering a hand-drawn special level crashed the machine
  to BASIC.** A code bank had silently outgrown its 16 KB, and the tape loader
  truncated it — clipping the template data. The 1.5.0 `.tap` is affected on
  roughly one in nine depths below 2; **128K players should update.** (The
  build now refuses to pack an oversized bank, so this cannot ship again.)
- A **dropped item now lies on the floor as itself** — a dropped potion showed
  the weapon tile (the map char was hard-coded to `)`), which made it look
  like some unrelated item from your pack.
- **Traps no longer generate inside shops** (a trap door mid-shopping was
  possible), matching NetHack's rule.

### Changed
- **Held-key movement now paces like a keyboard** (typematic repeat): a tap is
  exactly one step, and holding a direction walks steadily after a short
  beat. Fast turns used to fire 2-3 steps per tap, making it hard to stop on
  the cell you meant.
- **128K: walking corridors is now as fluid as walking inside rooms** — a
  corridor step repaints only the handful of cells the light touched, not the
  whole viewport.

## [1.5.0] — 2026-07-01

### Added
- **Wands** (`z`) — a new class of item (`/`) that zaps magic and carries a
  handful of charges. Five kinds turn up as you descend:
  - **striking** — a bolt that hammers the first monster in its path;
  - **cold** — a ray that freezes *every* monster along the line;
  - **sleep** — drops the first monster it hits into a doze;
  - **teleportation** — whisks the first monster away elsewhere on the level;
  - **digging** — bores straight down through the floor, dropping you to the
    next level (no aiming needed).

  Aim a beam in any of the eight directions; it flies over your pet and the
  shopkeeper before it acts, and each zap spends one charge.

### Changed
- **Big rooms on the ZX Spectrum 128K are now fluid.** Moving inside a large lit
  chamber used to lag a second or two per step; the 128K now repaints only what
  actually moved and lets monsters chase you by line of sight, so even a crowded
  cavern keeps up with the keyboard. (The Next was already fast.)

### Note
- Saved games from 1.4.x will **not** load in 1.5.0 — the new wand loot shifts
  dungeon generation, so the save format and the world both change. Finish any
  game in progress before upgrading.

## [1.4.1] — 2026-06-28

A polish pass over the 1.4.0 features.

### Changed
- Your **pet dog now heals slowly** over time (up to 12 HP), so a careful
  companion can be kept alive instead of inevitably wearing down.
- **`d` now drops** an item onto the floor (to be picked back up later), as well
  as selling it when you're inside a shop.

### Fixed
- A **scroll of teleportation** now always moves you to a *different* room — it
  could land you back in the same room, or even on your own square.
- Displacing your **pet onto a hidden trap** now springs it.
- You can no longer **drop or sell the Amulet of Yendor** (which could leave you
  able to "win" without carrying it).
- Several messages that overran the 128K's 32-column line are shortened, and a
  latent buffer overflow in the item-description code is fixed.

## [1.4.0] — 2026-06-28

### Added
- **A pet dog** — every game now starts you with a loyal dog. It heels at your
  side, bites adjacent monsters (and can be wounded or killed fighting them), and
  follows you up and down stairs, into shops and all. Walk into it to swap
  places, not attack.
- **Throwing** (`t`) — the first ranged attack. Pick a weapon and a direction and
  it flies down the line until it strikes the first monster or thuds into a wall;
  it then lies on the floor where it lands, so you can walk over and pick it back
  up. Throwing your wielded weapon asks for confirmation first.
- **Prayer** (`p`) — call on your god to fix your worst trouble (cure starvation,
  heal a critical wound, lift poison/blindness/confusion, or uncurse an item),
  with a cooldown between prayers. Pray while standing on an **altar** to lift the
  curses from everything you carry.
- **Search** (`s`) — search the cells around you to reveal hidden traps as `^`. A
  revealed trap still springs if you step on it, so mark it and step around.
- A **`?` help screen** listing every command, on both targets.

### Changed
- The Next build dropped its always-on two-line command bar for the shared `?`
  help screen (with a one-line pointer to it under the status bar), so the
  command list can grow without crowding the map. The `s` and `.` keys are now
  distinct — search vs. wait.
- Reclaimed the resident RAM the new features needed: moved the fog-of-war pool
  into Bank 5 and gave each target a third code bank for the monster AI.

## [1.3.0] — 2026-06-24

### Added
- Two deeper monsters: the **homunculus** (`i`, Dlvl 6+) whose bite can put you
  to sleep, and the **wraith** (`W`, Dlvl 8+) that drains your *maximum* HP.
- **Elbereth** — press `E` to scratch it in the dust; while you stand on a live
  engraving, adjacent monsters dare not strike (~30 turns, cleared on a level
  change).
- **Altars** (`_`) on some levels; step onto one to reveal the
  blessed/uncursed/cursed state of everything you carry.

### Changed
- Reclaimed ~420 B of resident RAM (the fog-of-war pool, `FOV_SLOTS` 8 → 6) to
  fit the new features in the tight Z80 budget.

### Fixed
- The altar message now fits the 128K's 32-column display (it was truncated).

## [1.2.0] — 2026-06-22

### Added
- **Status effects** — confusion, blindness, sleep/paralysis and poison, shown
  as tags on the status bar.
- **Item identification** — potions and scrolls start unidentified, shown by a
  per-game random appearance ("a blue potion", "a scroll labeled XYZZY") until
  you use one.
- **Blessed/uncursed/cursed** equipment — a hidden BUC state, revealed when you
  wear an item; blessed gear performs better, cursed gear is worse and welds
  itself on.
- **Monster special attacks** — snakes poison, leprechauns steal gold and
  vanish, yellow lights blind — plus more monster variety with depth.
- **Traps** — hidden trap doors, dart traps and sleeping-gas traps from Dlvl 2,
  sprung the first time you step on them.

### Changed
- Save format changed; saves from earlier versions are not compatible.

## [1.1.1] — 2026-06-21

### Changed
- ZX Spectrum 128K performance pass (no gameplay change; the Next build is
  functionally unchanged): hand-written Z80 ULA blits for text and map tiles, a
  status bar that redraws only changed cells, a tight Z80 fill for the
  monster-chase distance map, and a lighter held-key throttle (`in_pause`
  40 → 6 ms).

## [1.1.0] — 2026-06-20

### Added
- **ZX Spectrum 128K** target — ULA display, hand-drawn 1-bit map tiles, an
  edge-scrolling 32-column viewport over the 80-wide map, SCR title/victory
  screens, beeper sound, esxDOS save/restore (needs a DivMMC) and a `?` help
  screen; built from one dual-target source tree (code-banked via port `0x7FFD`).

### Changed
- Refreshed ZX Spectrum Next title-screen art.

## [1.0.1] — 2026-06-15

### Changed
- Shops, polished from playtesting: the shopkeeper greets you, shop walls are
  warm tan/brown brick and the keeper has his own orange-robed tile; buying asks
  for confirmation first; shops never hold a staircase and now stock ~4–8 items;
  bumping a wall re-announces the item you stand on.
- Save format changed (shop generation); v1.0.0 saves are not compatible.

## [1.0.0] — 2026-06-15

First public release — a from-scratch, NetHack-inspired roguelike for the
ZX Spectrum Next, written in C with z88dk.

### Added
- A **50-level dungeon**, generated procedurally and **deterministically** —
  each depth regenerates identically, while the changes you make are remembered
  across revisits.
- **Field of view** with fog of war; colourful 8×8 pixel-art tiles on the Next's
  hardware tilemap (80×32).
- **Turn-based combat** against a depth-scaled bestiary with BFS pathfinding; XP
  and experience levels.
- **Items and equipment** — weapons, armour, potions, food, scrolls and rings
  with enchantment and erosion.
- **Shops**, and **special levels**: the cavernous Big Room, guarded **treasure
  vaults**, and five **hand-drawn maps** (temple, maze, fortress, cavern, crypt).
- Hunger and HP regeneration, beeper sound effects, NetHack-style **save & quit**,
  and Layer 2 **title and victory screens**.
- The goal: bring the **Amulet of Yendor** up from the bottom of the dungeon.

[1.3.0]: https://github.com/lrrosa/nexthack/releases/tag/v1.3.0
[1.2.0]: https://github.com/lrrosa/nexthack/releases/tag/v1.2.0
[1.1.1]: https://github.com/lrrosa/nexthack/releases/tag/v1.1.1
[1.1.0]: https://github.com/lrrosa/nexthack/releases/tag/v1.1.0
[1.0.1]: https://github.com/lrrosa/nexthack/releases/tag/v1.0.1
[1.0.0]: https://github.com/lrrosa/nexthack/releases/tag/v1.0.0
