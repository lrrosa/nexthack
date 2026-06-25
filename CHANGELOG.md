# Changelog

All notable changes to NextHack are documented here.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and the project follows [Semantic Versioning](https://semver.org/spec/v2.0.0.html).
Every release ships two binaries — `nexthack.nex` (ZX Spectrum Next) and
`nexthack128.tap` (ZX Spectrum 128K) — on the
[Releases](https://github.com/lrrosa/nexthack/releases) page.

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
