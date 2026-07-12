# Changelog

All notable changes to NextHack are documented here.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and the project follows [Semantic Versioning](https://semver.org/spec/v2.0.0.html).
Every release ships two binaries — `nexthack.nex` (ZX Spectrum Next) and
`nexthack128.tap` (ZX Spectrum 128K) — on the
[Releases](https://github.com/lrrosa/nexthack/releases) page.

## [0.11.0] — 2026-07-11

The soul release: the dungeon stops charging at you — and starts behaving
like NetHack.

### Added
- **Half the dungeon is asleep.** Monsters now spawn asleep about half the
  time: they hold their ground until you come near (footsteps within 5 cells
  may stir them; standing next to one always does) — and a sleeping target
  can't dodge, so the **sneak attack always lands**. A sleeping dragon only
  snores. Stealth is a real option at last.
- **Minetown makes peace.** The gnomes and dwarves of Mine:2 go about their
  business: they amble around town, never strike first, and bumping one just
  swaps places, like your dog. A townsman is murdered by *choice* — a thrown
  blade, a wand, a spell — and first blood angers the **whole town** while
  the gods frown on you (luck −2, *"You murder the dwarf!"*). Your dog won't
  maul the townsfolk.
- **Monsters follow you on the stairs.** An awake enemy standing at your
  heel when you take the stairs (or the mine hole) comes along — *"The
  kobold follows you!"* — as wounded as you left it. Fleeing a fight now
  needs distance first, as in NetHack.
- **The nymph** (`n`, depth 7+). Her bite is soft, but it lifts an item from
  your pack — and she blinks away across the level with the prize. Kill her
  and it drops at her feet; leave the level and she abandons it on the
  floor, where the stash remembers it. Worn gear is strapped on.
- **Death drops.** About one kill in four leaves loot instead of a corpse —
  a depth-appropriate item where the monster fell. Your dog's kills count.

### Changed
- **Stairs you can finally read.** On the Next, up is a bright stone
  ziggurat rising toward you and down is banded treads sinking into a pit
  that darkens with depth; on the 128K they are stepped profiles — up
  climbs to a long top landing, down runs out onto a long floor, so the
  two never mirror each other.
- **The yellow light is not a coin.** Both targets now draw an irregular
  spark — uneven rays around a glowing core (orange on the Next); the gold
  piece stays a plain disc.
- **The mines are built of stones.** Mine walls became rounded cobblestones
  on both targets — brick-laid, dark joints, the odd tan glint — instead of
  uniform rough noise.
- **Minetown's shop wears its bricks.** The warm shop-wall override only
  matched regular walls, so in the mines the shop drew as plain rock on
  both targets. Fixed — the shop now glows warm against the caves.
- Small touches: the dog's eye sits properly in its head on both targets,
  the rat grew an ear, a head and a raised tail (it was a headless blob),
  and the 128K altar is a classic temple front — serifed top bar on two
  columns.

### Note — the save freeze holds
- Saves from 0.10.0 **load unchanged**. Everything above is transient
  monster state or a pure side hash: the save format and the deterministic
  generation streams are untouched, exactly as promised at the freeze.

## [0.10.0] — 2026-07-08

The depths release: a branch off the beaten shaft — and the LAST save-breaking
release before 1.0.

### Added
- **The Gnomish Mines.** A dark hole waits somewhere on the floor of Dlvl 2:
  `>` on it descends into a four-level side branch owned by **gnomes and
  dwarves** — and it *looks* the part: chambers hewn from brown rock instead
  of built brick, winding hand-dug tunnels, pillars of stone left standing,
  and gold everywhere. **Mine:2 is Minetown**, a shop trading in the middle
  of the caves; at the bottom rests the **luckstone**, which steadies your
  luck by +2 while you carry it (your sword arm and your prayers both feel
  it). The status bar reads `Mine:1-4`, and climbing out of Mine:1 emerges
  from the very hole you entered.
- **Your stash waits for you.** Dropped and thrown items — and the corpses of
  the fallen — now persist: the floor of the 8 most recently left levels is
  remembered, and remembered by the save file too. A pile of spares by the
  stairs is a real strategy at last.
- **Conducts.** The score screen now honours restraint: end your run without
  ever killing by your own hand, eating flesh, reading a word, or petitioning
  a god, and it says so — **Pacifist, Vegetarian, Illiterate, Atheist**.
  (Your dog's kills don't stain your pacifism.)
- **A longer memory.** The fog of war now remembers the **12** most recently
  visited levels (was 4), and the pack grew to **26 slots** — one per menu
  letter, a..z.

### Changed
- **A visual pass across both targets.** The hero is finally an armoured
  adventurer — helm, blue mail, shield and a raised sword on the Next, a
  raised blade on the 128K's figure. The dog gained a real head, an eye and
  a thin wagging tail on both machines, and the 128K's doors got their arch.
  Fresh README screenshots throughout.
- The build targets a fresh z88dk nightly (now expected at `../z88dk`);
  verified byte-comparable on both targets.

### Note — the save format freezes here
- Saved games from 0.9.x will **not** load — the dungeon grew a branch, the
  pack grew, the fog pool tripled. **This is the last save-breaking release:
  from here through 1.0, saves stay compatible.**

## [0.9.0] — 2026-07-08

The gauntlet release: the dungeon fights back — and hands you a bigger arsenal.

### Added
- **The ascent is a gauntlet.** With the Amulet in your pack the dungeon sends
  its deep servants after you no matter how near the surface — wraiths and
  trolls chase you to the daylight instead of rats — and **Moloch drowns out
  your prayers** entirely. (Wanderers already spawned ~3× faster on the
  ascent; now they have teeth.)
- **The mimic.** From depth 6 the dungeon baits its floors: that "potion" may
  be a monster wearing the potion's tile. Step next to it or strike it and it
  sheds the disguise as a toothy chest that bites back the same turn — and
  your dog cannot smell a posing mimic.
- **Dragons breathe fire** down a clear straight line (range 6). Walls, doors
  and creatures block the blast — your own dog can shield you — armour soaks
  half, and fire outranges Elbereth. At depth 22+ corridors suddenly matter.
- **Your puppy grows.** At 4 and at 12 lifetime kills the dog gains a size:
  a harder bite, +6 HP on the spot, and a deeper regeneration cap.
- **New scrolls** — **enchant weapon** and **enchant armor** (+1 and shine
  the rust off the piece in use) and **remove curse** (lifts the whole pack).
- **Potion of gain level** — one whole experience level, bottled.
- **Ring of regeneration** — worn, you mend twice as fast.
- The unidentified looks grew with the catalogue: six potion colours and six
  scroll labels, all unique within a game.

### Changed
- **~3 KB of RAM reclaimed on both targets.** The item catalogue and all its
  texts moved into a memory bank of their own (the Next gains bank 14; the
  128K tape adds a fifth code block to its loader). Invisible in play — it is
  the room the next features will breathe in.

### Note
- Saved games from 0.8.x will **not** load — the catalogue and the pet's
  record grew. Finish any run in progress before upgrading.

## [0.8.0] — 2026-07-05

The divine release: your god is watching — and the deep dungeon bites back.

### Added
- **Alignment and sacrifice.** Every class now serves a god — the Valkyrie is
  Lawful, the Wizard and Tourist Neutral, the Rogue Chaotic — and every altar
  (`_`) has an alignment of its own (shown when you stand on one). **Drop a
  corpse on an altar to offer it** (`d`): a co-aligned god always accepts, and
  grants the richer boons; a crossed one may spurn the gift. Boons: your curses
  lifted, your weapon blessed and sharpened, body and spirit restored, or a
  lasting gain to max HP or max Pw.
- **Luck.** A hidden fortune the gods adjust: pleased offerings raise it,
  spurned ones lower it. It quietly sways your swings — and a god ignores the
  prayers of the forsaken, so stay in favour.
- **Altars appraise gifts.** Drop anything on an altar and a flash names it:
  blue for blessed, black for cursed, none for plain. A **potion** takes the
  altar's own touch — blessed on a co-aligned altar, *cursed* on a crossed one
  — the poor man's holy water. (Items can now rest on altars, thrown weapons
  included.)
- **The deep roster.** Depths past 12 no longer recycle the shallow bestiary
  with inflated HP: the **troll** knits its wounds shut every turn you fight
  it, the **vampire** drains your life force, and the **dragon** (Dlvl 22+) is
  the apex predator — though its corpse hardens your blood against poison.
- **The Sanctum has a keeper.** A **high priest** — the hardest hitter in the
  game — stands on the Amulet of Yendor itself. *"A terrible presence dwells
  here."* Slay it once and the Sanctum stays yours.

### Changed
- **Holding a direction on the Next walks noticeably faster** (a tap is still
  exactly one step), and a held walk no longer stutters mid-stride.

### Note
- Saved games from 0.7.x will **not** load — the player block grew (alignment,
  luck). Finish any run in progress before upgrading.

## [0.7.0] — 2026-07-04

The arcane release: spellbooks, fountains, and a score to beat.

### Added
- **Spellbooks and spellcasting.** A new class of item (`&`); **read** (`r`)
  one to learn its spell, then **cast** (`Z`) from a menu that spends spell
  power (Pw): a **force bolt**, **healing**, a **sleep** ray, or
  **teleportation**. Casting can fizzle if your mind is weak, so the Wizard
  (who starts with the spellbook of force bolt) rarely fails where a Valkyrie
  often would. Pw is real now — it regenerates over time (faster with Wisdom)
  and grows as you level up.
- **Fountains** (`{`) dot the deeper floors. Drink from one (`q`) for cool
  water, foul water, a handful of coins, or a fountain that dries up — and a
  Valkyrie of experience level 5 or more, wielding a long sword, may have the
  Lady of the Lake hand up **Excalibur**. (The Valkyrie now starts with a long
  sword, as in NetHack, to make that reachable.)
- **A score screen** on death or victory: who you were, how deep you reached,
  your turns, gold and a final score — weighed against the best run so far,
  which persists between games.

### Fixed
- **ZX Spectrum 128K: `S` save now works under ZEsarUX.** The game detects the
  emulator's esxDOS handler through a new guarded probe, and `run-zx128.bat`
  enables it. (A machine with no esxDOS still runs; it just can't save.)
- **Opening the inventory or help no longer leaves a stray letter on a
  monster** when the screen redraws.

### Note
- Saved games from 0.6.x will **not** load — the character sheet grew and the
  new items shift world generation. Finish any run in progress before
  upgrading.

## [0.6.0] — 2026-07-04

The NetHack-identity release: who you are finally matters.

### Added
- **Classes** — every new game asks *who are you?* Four ways into the dungeon:
  - **Valkyrie**: St 17, 16 HP, short sword and ring mail. Hits hard, mends
    fast, haggles badly.
  - **Wizard**: In 16, 10 HP, 6 Pw; a dagger, a charged wand of striking and a
    healing potion.
  - **Rogue**: Dx 16, 12 HP, dagger, leather armor and 60 gold. Almost never
    misses.
  - **Tourist**: Ch 14, 12 HP, two food rations, a potion and 120 gold. Eats
    well, pays less in shops, fights like a tourist.
- **Attributes have real effects** — the status bar's numbers are no longer
  decoration: Dexterity decides whether a swing lands ("You miss the rat."),
  Strength adds melee damage, Constitution speeds regeneration and level-up
  HP, and Charisma haggles shop prices up or down.
- **Corpses** — slain monsters may leave one, and *you are what you eat*:
  poisonous flesh can grant **poison resistance** (or poison you), a
  homunculus can grant **sleep resistance**, and a floating eye's corpse
  grants **telepathy** — sense every monster on the level while blind.
- **The floating eye** — a blue lidless orb from depth 5 that neither chases
  nor bites. Strike it in melee with your eyes open, though, and its gaze
  **freezes you** while everything else closes in. Blind heroes are immune.
- **Scroll of identify** — reading it names every carried item and reveals
  each one's blessed/cursed state.

### Fixed
- **Your dog no longer gets stuck in shops.** Two bugs: the 128K dog could
  "heel" at your side *through a shop wall*, and the shopkeeper — displaced
  when you walk past him — could end up parked on the door cell forever,
  sealing the shop. The pet now displaces the shopkeeper, as pets displace
  peacefuls in NetHack.

### Note
- Saved games from 0.5.x will **not** load — the character sheet joins the
  save and the new items shift world generation. Finish any run in progress
  before upgrading.

## [0.5.1] — 2026-07-03

A hotfix + polish pass over 0.5.0. Saved games from 0.5.0 still load.

### Fixed
- **ZX Spectrum 128K: entering a hand-drawn special level crashed the machine
  to BASIC.** A code bank had silently outgrown its 16 KB, and the tape loader
  truncated it — clipping the template data. The 0.5.0 `.tap` is affected on
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

## [0.5.0] — 2026-07-01

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
- Saved games from 0.4.x will **not** load in 0.5.0 — the new wand loot shifts
  dungeon generation, so the save format and the world both change. Finish any
  game in progress before upgrading.

## [0.4.1] — 2026-06-28

A polish pass over the 0.4.0 features.

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

## [0.4.0] — 2026-06-28

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

## [0.3.0] — 2026-06-24

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

## [0.2.0] — 2026-06-22

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

## [0.1.1] — 2026-06-21

### Changed
- ZX Spectrum 128K performance pass (no gameplay change; the Next build is
  functionally unchanged): hand-written Z80 ULA blits for text and map tiles, a
  status bar that redraws only changed cells, a tight Z80 fill for the
  monster-chase distance map, and a lighter held-key throttle (`in_pause`
  40 → 6 ms).

## [0.1.0] — 2026-06-20

### Added
- **ZX Spectrum 128K** target — ULA display, hand-drawn 1-bit map tiles, an
  edge-scrolling 32-column viewport over the 80-wide map, SCR title/victory
  screens, beeper sound, esxDOS save/restore (needs a DivMMC) and a `?` help
  screen; built from one dual-target source tree (code-banked via port `0x7FFD`).

### Changed
- Refreshed ZX Spectrum Next title-screen art.

## [0.0.1] — 2026-06-15

### Changed
- Shops, polished from playtesting: the shopkeeper greets you, shop walls are
  warm tan/brown brick and the keeper has his own orange-robed tile; buying asks
  for confirmation first; shops never hold a staircase and now stock ~4–8 items;
  bumping a wall re-announces the item you stand on.
- Save format changed (shop generation); v0.0.0 saves are not compatible.

## [0.0.0] — 2026-06-15

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

[0.3.0]: https://github.com/lrrosa/nexthack/releases/tag/v0.3.0
[0.2.0]: https://github.com/lrrosa/nexthack/releases/tag/v0.2.0
[0.1.1]: https://github.com/lrrosa/nexthack/releases/tag/v0.1.1
[0.1.0]: https://github.com/lrrosa/nexthack/releases/tag/v0.1.0
[0.0.1]: https://github.com/lrrosa/nexthack/releases/tag/v0.0.1
[0.0.0]: https://github.com/lrrosa/nexthack/releases/tag/v0.0.0
