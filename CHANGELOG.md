# Changelog

All notable changes to NextHack are documented here.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and the project follows [Semantic Versioning](https://semver.org/spec/v2.0.0.html).
Every release ships two binaries — `nexthack.nex` (ZX Spectrum Next) and
`nexthack128.tap` (ZX Spectrum 128K) — on the
[Releases](https://github.com/lrrosa/nexthack/releases) page.

## [1.9.1] — 2026-10-25

**The dog keeps up, the doors stay open, and seven bugs from the review are
gone.** On the 128K the dog no longer gets lost behind a wall; a door you
have walked through is drawn as an open doorway from then on; the hit
points warn in colour as hunger does. Seven issues reported against 1.9.0
(#19–#25) are fixed, each reproduced on the 1.9.0 binary and proven fixed in
ZEsarUX on both machines. **1.9.0 saves load** — with every door still shut.

### Fixed
- **128K: the dog no longer gets lost.** With no enemy near, the dog heads
  for you by sight, and when a wall stopped it, it took one step along the
  way round, then its nose pulled it back. Leaving a room by one door, it
  could run along the inside wall, leave by the other and stop against the
  wall nearest you, too far round for its pathfinding to reach — for good.
  Now once a wall stops it, it follows the path until it is at your heel,
  however far behind it is. The Next never had the problem.
- **Two of a kind in the pack** (#19): a second ring like the first can be
  put on (`P` offers only what you are not wearing), and two wands, potions
  or scrolls of one type get the letter menu, so the charged wand or the
  blessed potion can be chosen.
- **A full pack and a scroll of scare monster** (#20): picking one up with
  every letter taken (it joins a stack) now turns a touched or cursed one to
  dust and strips a blessing, as with room in the pack.
- **The quantum mechanic and Elbereth** (#21): flung onto an Elbereth by its
  touch, you are protected there for the rest of the turn.
- **The throne's curse** (#22) costs the cursed weapon or armour its bonus at
  once, not at the next change of gear.
- **128K: long names in shops** (#23): the price no longer spills into the
  next row; the name gives way.
- **A restore and the monsters** (#24): a monster whose spawn square you
  saved on steps aside instead of coming back on top of you.
- **A restore and Elbereth** (#25): the engraving under you — burned or in
  the dust, with its turns left — survives `S` and a restore. Changing level
  still wipes it.

### Added
- **Doors stay open.** A door you walk through becomes an empty doorway, on
  that visit and every later one, saved with the game. It is still a door to
  everything else: you cannot see through it, dig it, or land a teleport on
  it.
- **Hit points in colour**: yellow at half or less, red once a prayer would
  heal you — the same warning the hunger line gives.

### Changed
- `SAVE_VER` 40: the save keeps the open doors and the engraving underfoot.
  **1.9.0 saves load** (doors shut, no engraving), and 1.9.0 names a 1.9.1
  save as another version's and asks — proven both ways on both machines
  against the 1.9.0 release binaries.

### Under the hood
- The map cursor (farlook and teleport control's pick) moved out of
  `nexthack.c` into `farlook.c`: the 128K's bank for it was down to 132 B.
- On the Next, `nexthack_lvl` moved to bank 10 (`PAGE_20`) when the restore
  code filled bank 15.
- A door test in the code is `IS_DOOR` now, so an open doorway still counts
  as a door; the fuzzer draws it as the game does.

## [1.9.0] — 2026-10-24

**Eight classes, a faster and slower dungeon, and a pack that stacks.**
Four more of NetHack's classes, ten more of its monsters, and the speed that
sets them apart — a zombie walks two squares while you take three, a bat
nearly two for each of yours. Potions, scrolls, food and daggers share a
letter now, `T` takes things off, and the hero has two hands for rings.
Every change was measured in `tools/balance.py` before it went in, and the
difficulty stays where it was: NetHack's order, the Valkyrie and the Samurai
the easiest starts, the Healer and the Tourist the hardest. All on both
machines, every piece proven in ZEsarUX and under the MAME fuzzer.
**Saves from 1.8 cannot be loaded** (the pack's format changed): 1.9 names
such a file and asks before deleting it, and 1.8 does the same with a 1.9
save.

### Added
- **Four classes**: the **Samurai** (long sword and splint mail, Lawful,
  born *Fast*), the **Barbarian** (a two-handed sword and ring mail,
  Chaotic, born resisting poison), the **Priest** (a mace, the healing
  spellbook, and he sees whether *anything* is blessed or cursed — in the
  pack, on the floor, in a shop) and the **Healer** (a scalpel, the healing
  spellbook, a potion, a wand of sleep, a full purse, and resistance to
  poison). The picker lists eight.
- **Ten monsters**, each drawn twice for the two machines: the **unicorn**
  (it charges, and leaves its horn when slain), the **rothe**, the
  **quantum mechanic** (its touch flings you across the level, and eating
  one toggles your speed), the **rust monster** and the **black pudding**
  (they corrode your gear, as the acid blob does), the **ogre**, the
  **giant spider** (poison), the **umber hulk** (its gaze confuses), the
  **hill giant** and the **lich** (it drains like a wraith), from Dlvl 4 to
  Dlvl 24.
- **Speed.** Every monster moves at its own pace, NetHack's way: it banks
  its speed each turn and acts once for every twelve it holds. Zombies,
  gnomes, dwarves, blobs, mimics and puddings are slow; bats, snakes,
  leprechauns, rust monsters and unicorns are quick. A **Fast** hero gets a
  free turn in four. The new **wand of speed monster** makes you Fast for
  good if you zap it at yourself (`.` at the direction prompt) — or makes a
  monster faster, if you aim it at one. Engraving with it names it: the
  bugs on the floor speed up.
- **Stacks.** Potions, scrolls, food (corpses of the same monster) and
  daggers merge when they are alike in every way, blessing included, and
  are counted in the name: *3 potions of healing*, *2 scrolls labeled
  XYZZY*, *12 daggers*. A full pack still takes a potion it already holds.
  Drinking, eating, reading, throwing and offering use one; dropping and
  selling take the whole stack; daggers thrown the same way land in one
  pile.
- **`T` takes things off** — armour, a ring, the amulet, the blindfold, or
  the weapon in your hand. A cursed piece stays on and says so, at no cost
  in time.
- **Two ring hands.** `P` puts a second ring on the other hand; with both
  full it says so, and you take one off first, as in NetHack — the cursed
  one cannot be the one that makes way.
- **Potion of restore ability**, from Dlvl 5: it gives back what a cursed
  throne drained — one attribute, or every one if the potion is blessed. A
  gain in an attribute raises its peak; a drain never lowers the peak, and
  the peak is what the potion restores.

### Changed
- **`P` no longer swaps** the ring or amulet you wear for the new one: take
  it off with `T`, then put the other on.
- **The dungeon's mix**: a depth's monsters are drawn with weights, and the
  ten new ones only for ten levels from their first — NetHack's own
  levers. Added plainly, they would have made the deep levels several
  times deadlier, because every bite grows with depth.
- **Monster speeds are NetHack's, at half the spread** for the slow ones
  (a zombie 8 instead of 6), and the dog — the jackal-sized `d`, and your
  pet with it — keeps the normal 12. NetHack's own table made the game far
  easier: the dog alone at 16 lifted the Valkyrie's modelled wins by
  almost thirty points.
- Modelled wins (`tools/balance.py`, with the dog, five seeds of 600
  lives): Samurai 66%, Valkyrie 63%, Barbarian 61%, Priest 51%, Wizard
  44%, Rogue 36%, Tourist 19%, Healer 17% — the first four were 59/43/35/19
  before, and stay there.
- `SAVE_VER` 38: the pack's objects grew a count. **1.8 saves get the
  incompatible-save prompt** (`n` keeps the file for 1.8), and 1.8 asks the
  same about a 1.9 save — proven both ways on both machines against the
  1.8.1 binary.
- The help screen lists `T`; the README lists the new classes, monsters and
  tiles.

### Under the hood
- **`item.c` split again**: the verbs that put gear on and take it off
  went to `item_wear.c`, when its bank was down to 157 B (Next) and 37 B
  (128K) with the take-off command and stacks still to come.
- `montypes[]` has three more columns — speed, generation weight and the
  deepest level a type spawns at — and `tools/balance.py` reads all three
  out of the C and draws exactly what the game draws.
- `balance.py` plays the classes the way the game lets them: a Fast hero's
  free turn runs on a clock across fights, as the game's turn counter does,
  and a hurt hero who knows healing casts it between fights.
- `tools/bankmap.py` counts `obj_t` out of `item_int.h` instead of
  assuming five bytes — the 128K's pack now ends 4 B short of the BFS
  scratch.
- The fuzzer checks stack counts and that every level's own monsters keep
  to their depth window, and its walk check now starts from the up stairs:
  a teleport into a room only a secret door leads to is legal, and it used
  to be flagged as a soft lock.
- The test harness starts ZEsarUX without a console window.

## [1.8.1] — 2026-10-19

**Seven reported bugs, fixed.** A review of 1.8.0 reported them on GitHub
(#12–#18); each one was reproduced on the 1.8.0 code first — in ZEsarUX
on both machines for the game's five, by simulating the failure for the
build scripts' — then fixed and proven on both. The fuzzer played both
binaries again and found nothing, and the 128K enters a level a little
faster. Saves from 1.8.0 load, and 1.8.0 loads saves from 1.8.1.

### Fixed
- **Taking the stairs takes a turn** (#12). The turn counter moved, but
  nothing else did: no hunger, no healing, no timers, and no answer from
  the monsters — up, down, into the Mines and out. Now the stairs are a
  move like any other, as a trap door's fall always was; a refused climb
  still costs nothing.
- **A monster that follows you down the stairs and dies there stays dead**
  (#13). Its old level spawned it again on your next visit, and its
  experience could be had again on every trip.
- **A ring the sink gives back can no longer vanish** (#14). On a floor
  already holding as much as it can (and no corpse to make way), the ring
  that "came back up" was gone. Now you fish it back out.
- **Genocide no longer reshuffles the levels you have visited** (#15). A
  level already seen grew other monsters in other places after a genocide,
  and a monster you had killed there could come back while a different one
  went missing. Every level now keeps its own monsters, minus the
  genocided kind.
- **128K: a corridor step no longer leaves the corridor dark** (#18). The
  screen decided whether your sight had changed from a 16-bit checksum,
  and two different views can share one: the newly lit corridor was then
  not drawn until something else repainted it. The test is now exact, and
  costs about what the checksum did.

### Changed
- **The Amulet's high priest no longer follows you up the stairs** — a
  temple's priest keeps his temple, as in NetHack. Lured away and left on
  another level, he would now have been gone for good.
- A monster that followed you and was **left behind on another level** is
  gone when you leave that level, instead of walking back home. A follower
  still acts from your next move, unless your dog goes for it first.
- **The 128K enters a level faster** — 0.32-0.39 s instead of 0.34-0.44
  (MAME, same world): the flood that keeps secret doors off the way between
  the stairs is hand-written Z80 now, square for square the C the Next
  keeps.
- `SAVE_VER` stays 36: **1.8.0 saves load**, and 1.8.0 loads saves from
  1.8.1.

### Under the hood
- **The builds fail when the link or the tape fails** (#16). A failed link
  used to end in "OK" over the previous build's `.nex` or `.tap`, and the
  128K's tape was then rebuilt from the previous build's banks. The old
  outputs are deleted first now, and every step's exit code is checked.
- **`tools/bankpack.py` searches exactly** (#17): it can move a module out
  of a bank that fits to make room, so a swap is no longer reported as "a
  real budget wall"; it says a placement is impossible only once it has
  proved it. `bankpack.py selftest` checks it against brute force, in CI.
- The Next no longer works out, every turn, the checksum of #18 that only
  the 128K ever read.

## [1.8.0] — 2026-10-17

**The dungeon's furniture and its secrets.** Thrones to sit on, sinks to
drink from and drop rings down, graves to read and dig up; doors and
corridors hidden in the walls for `s` to find; shops that specialise, as
NetHack's do; and wands that give themselves away when you engrave with
them. All NetHack, all on both machines, every piece proven in ZEsarUX and
under the MAME fuzzer. It was paid for by memory the game never used: the
C runtime's text terminal, 3.8 KB of the resident half on each machine.
Saves from 1.7 load, and 1.7 loads saves from 1.8.

### Added
- **Thrones** (`\`), one level in ten from Dlvl 5. **Sit** with `#`: two
  times in three nothing happens; otherwise one of NetHack's thirteen
  effects — a cursed throne's drain, an attribute gained, a massive shock,
  full healing, your gold gone, your luck changing, a court summoned around
  you, a genocide, a curse (or blindness, if your luck holds), the level's
  map, a teleport (or every sleeper woken, if your luck is bad), an insight
  into your pack, or a mind turned into a pretzel. After every sitting, a
  third of the time the throne vanishes. No wish: luck takes its place.
  Sitting anywhere else says where you sat.
- **Sinks** (`~`), one level in seven. `q` on one drinks: cold, warm or
  scalding water, a sewer rat, the sink's one ring, sewage, noises from the
  pipes. `K` beside one mostly rattles — once, it gives up its ring, or
  something muddy climbs out. **Drop a ring down it** (`d`) and the sink
  does something that names the ring — glows silver, looks as good as new,
  flies buzz, it vanishes — at the price of the ring. Slow digestion comes
  back up, hunger shows nothing, and one time in twenty the sink backs up.
- **Graves** (`` ` ``), likelier the deeper you go, each with an epitaph,
  shown when you step on one or look at it. Dig one up — a pick-axe down,
  or a zapped wand of digging, which then makes no hole — and find a
  corpse, an angry owner (a zombie, or a wraith from Dlvl 8) or nothing,
  sometimes with a cursed keepsake and a little gold. A lawful hero who does
  it has desecrated a grave, and the gods notice.
- **Secret doors and secret corridors.** One door in eight hides behind a
  stretch of wall, and the odd square of a straight corridor behind rock,
  as in NetHack, from Dlvl 2 — but, unlike NetHack, never on the way from
  the up stairs to the down stairs (or to the Mines): side rooms, vaults
  and branches can hide; the way through cannot. `s` turns one up a try in
  five (better with good luck), and the level remembers what you found. A
  blessed scroll of magic mapping shows the secret doors for what they are.
- **Specialised shops**: NetHack's ten kinds by NetHack's odds — the
  general store still most often, then the used-armour dealer, the
  bookstore, the liquor emporium, the weapons outlet, the delicatessen, the
  jeweler, "quality apparel" (NetHack's wand shop), the hardware store and
  rare books. Walking in names the shop, and a specialist buys only its own
  line ("The shopkeeper is uninterested."). Minetown's stays a general
  store.
- **Engraving with a wand.** `E` asks whether to write with a wand, and a
  wand that writes leaves its mark and spends a charge: gravel flies
  (digging), flames (fire), ice cubes (cold), bugs stop moving (sleep),
  bullet holes (magic missile), the wand fights your writing (striking), an
  old engraving vanishes (teleportation). Each mark names its wand. Fire
  burns Elbereth in for the rest of the visit, and digging carves it to last
  longer than the dust.

### Changed
- **A blow struck from Elbereth rubs it out**, burned or not, as in NetHack
  3.6. A burned Elbereth that lasts the whole visit would otherwise be a
  fortress to fight from forever; this goes for the dust too, which until
  now could be fought from for its whole life.
- **Entering a level takes about a tenth of a second longer on the 128K**
  (0.34-0.44 s instead of 0.24-0.29, timed in MAME): the flood that keeps a
  secret from cutting the stairs off. The Next pays an eighth of that.
- The help screen and the README list `#` (sit), what `s` and `K` find now,
  and the new tiles.
- `SAVE_VER` stays 36: **1.7 saves load**, and a 1.8 save loads in 1.7 —
  both proven on both machines against the 1.7.0 binary, rebuilt byte for
  byte. What 1.8 remembers that 1.7 does not — found secret doors, a sink's
  taken ring — rides the dug-cell pool in records 1.7 skips.

### Under the hood
- **3.8 KB of resident memory back** on both machines: the link used z88dk's
  startup 1, which is its C runtime plus a 32-column stdin/stdout terminal
  the game never called (it draws its own screen and reads keys through
  `in_inkey`). `-startup=31` is the same runtime without it, and the stdio
  and malloc heaps went too. The Next had 189 bytes left before its stack;
  it has 3.7 KB now.
- **The 128K's title and victory screens are LZ-packed** (the templates'
  format), unpacked straight onto the screen: 6.7 KB of bank freed, which
  is where the furniture lives. `lz_expand` moved into the resident half to
  serve both. The title on screen was checked byte for byte against the
  unpacked original.
- The Next opened a sixth code bank (bank 22, `PAGE_44_CODE`) for the
  furniture and the one-time setup code.
- `build-zx128.ps1` compiles in parallel, as `build.ps1` does: a clean 128K
  build takes about 4 minutes instead of 28.
- NetHack's `--More--` for two-part news (`msg_more`): on the 128K it
  borrows the right end of the map's top row and repaints it afterwards.

## [1.7.0] — 2026-10-12

**A new look for the dungeon, and the two-handed sword.** The game speaks in
a gothic hand of its own instead of the Spectrum's ROM font, the
128K's walls get colour behind them for the first time, a door looks like a
dungeon door on both machines, and the Next's monsters borrow a second
palette for the colours its first never had. Every look was chosen from
previews of the real screens, and every change proven in ZEsarUX on both
machines. Saves from 1.6 load.

### Added
- **A gothic font on both machines**, based on **Byteletter** by DamienG
  (ZX Origins): textura lowercase, and capitals redrawn for NextHack so that
  "HP", "AC", "Dlvl" and a scroll's random label read at a glance — the
  originals are Fraktur, where an A passes for a U and a T for a C. Its `l`
  no longer passes for a `1`, its `i` for a `j`, and the punctuation is bold
  enough to read on the 128K. The ROM font is not used any more: the Next
  expands this one into its text tiles at boot (which also makes the bare
  `.nex` readable in MAME, which has no ROM font to copy), and the 128K
  copies it into Bank 5, where the tape loader's BASIC lived — no resident
  memory spent. The glyphs live in `tools/font/nexthack.txt`
  (`tools/txt2font.py` makes `src/font_data.h`).
- **The two-handed sword**, NetHack's *montante*: the first rung of the
  two-handed ladder, a 6 from Dlvl 16 — before the battle-axe (20) and the
  mattock (28) — for a hero ready to give up the shield early. Modelled with
  the dog over 3000 runs a class, it moves no class's wins by more than the
  noise (the Valkyrie 59.4% → 58.7%; one who trades the shield 82.9% →
  83.6%); a 7 from Dlvl 24 would have put the trader at 88%.
- **A second palette for monsters on the Next.** A text colour uses only two
  of its palette's sixteen entries; the spare fourteen of one now hold the
  purple, pink and olive the tiles never had. The **nymph** wears a pink gown
  (she was the leprechaun's green twin), the **high priest** a violet robe
  (his red was the dragon's), the **snake** is olive — NetHack's brown, out
  of the greens — and the **vampire**'s cape is deep purple. The 128K
  already drew the nymph and the priest in magenta.

### Changed
- **The 128K's walls have a paper colour.** Every map cell was ink on black;
  now the dungeon's walls are white bricks on blue, a shop's walls yellow on
  red, the fountain cyan on blue water, and a door a red plank framed in
  black — the one frame that stands out in all three walls a door can sit in
  (yellow sank into the shop's bricks). The mines keep their yellow on
  black, so Minetown's shop stands out of the cave. Floors, items and
  monsters stay on black, and a remembered wall is still the dim one. The
  four colours are bytes in a table the renderer already read whole: no
  size, speed or save change.
- **A dungeon door on both machines.** The door was a plain arch with a
  knob; it is now an arched recess in the wall, two planks parted by a seam
  and a ring — black on red on the 128K, and on the Next brown planks with a
  pale grain and a brass ring in a dark recess, where the old grey frame and
  brown door had sunk into the shop's and the mines' browns. Same bytes.
- `SAVE_VER` 36, but **1.6 saves still load**: the bytes are the same. The
  new version number is for the other direction — a 1.6.x binary would read
  a pack holding a two-handed sword past the end of its catalogue, so it now
  sees another version's save and asks, instead of loading it. Both proven
  in ZEsarUX on both machines against the 1.6.2 binary: its save restored by
  this one, and this one's save refused by it and kept on `n`.
- The help screen lists `a apply` before `, pick up`: the comma no longer
  sits right above the `i` of inventory, where in the new font it was hard to
  tell which key was which.
- The test harnesses (ZEsarUX's `zrcp`, MAME's `mame.lua` and the fuzzer)
  read the 128K's text through the font the build prints with — the game's
  own at `_font_ram`, or the ROM's for a build before 1.7.
- The fuzzer's screen oracle on the Next learns the second palette, reading
  `tile_pal2()` out of `platform.h`: the release fuzz flagged every snake and
  nymph as drawn wrong until it did. `run-next.bat` and the `zrcp` harness
  keep ZEsarUX's ZX Desktop off (`--forbid-zxdesktop`), which the shared
  config otherwise remembered from one run to the next.

## [1.6.2] — 2026-10-11

**Three bugs a fuzzer found.** The game was played by a machine for hours in
MAME — random keys, the game's own rules checked after every one, and some
2,600 levels of random worlds visited — and these are what it turned up.
Every fix was reproduced on the old binary first, then proven on both
machines.

### Fixed
- **A dried fountain stays dry.** One drink in six dries a fountain up — until
  you left the level: it was full again on the next visit, so a fountain was
  endless gold and healing. The level now remembers it, through a save too.
- **No more altars or fountains appearing out of nowhere.** When gold or an
  item happened to lie on the spot where a level's altar or fountain would
  stand, the level had none — until you picked the pile up: on your next visit
  one stood there. A looted treasure vault was the likeliest place.
- **128K: monsters you sensed while blind no longer linger on screen.** Blind
  with telepathy, you see every monster on the level; when your sight came
  back through a prayer — or you traded away the amulet of ESP while blind —
  a monster that had not moved stayed drawn where you could not see it. The
  Next was never affected.

### Changed
- Two code banks rebalanced so none is left nearly full (on the Next the
  special-level loader moved to another bank, on the 128K the level builder),
  and `tools/bankpack.py --free` now picks the move that leaves *every* bank it
  touches roomiest, instead of curing one tight bank by making another.
- The `zrcp-verify` skill gains the **fuzzer** that found all of this
  (`fuzz/`): it plays the real `.nex`/`.tap` headless in MAME, checks the
  game's invariants at every key, watches the memory the game must never
  write, compares the screen with what the map says, and can visit random
  levels of random worlds to check that every staircase, the Mines' entrance
  and the Amulet can be reached on foot (all 2,600 could).
- Saves are unchanged (`SAVE_VER` 35): a 1.6.1 save loads in 1.6.2 — saved by
  the 1.6.1 binary and restored by this one in ZEsarUX, on both machines.

## [1.6.1] — 2026-10-06

**The 128K walks as fast as the Next.** Measured in MAME on the same world,
holding a direction key: a step every 140-160 ms in a room and 180 ms in a
corridor became 100 ms in both — the Next's ~107 ms — and an edge-scroll no
longer stalls for 0.6 s.

### Changed
- **The 128K's key repeat counts from the step, not from the redraw.** A
  held key used to wait its whole beat after the turn had been drawn, so every
  step cost the turn *plus* the beat; now the turn spends the beat, and a
  hold steps every 100 ms (220 ms before the first repeat, the Next's 212).
  Taps are still one step each, and a tap made during a scroll is no longer
  lost behind its redraw.
- **The 128K's edge-scroll is ~5x faster** (0.6 s → ~0.1 s): the viewport's
  terrain sweep is hand-written Z80, and a scroll repaints only the cells the
  new origin changes instead of all 672.
- **Faster turns on the 128K**: the field of view casts its rays without
  multiplying (on both machines), the status bar prints its numbers without
  dividing and skips its first row when depth, gold and HP are unchanged, a
  blank message line is no longer redrawn, and the chase's flood fill — run
  when a wall boxes a monster in — is hand-written Z80 too. A turn in a room
  takes ~40 ms instead of ~75; a turn with a monster boxed in, 95-120 ms
  instead of 140-195.
- The `zrcp-verify` skill's MAME harness can time: a PC sampler on emulated
  time, held keys, a watch list, a pinned world for A/B runs between builds,
  and `mameprof.py` to read the result.
- Saves are unchanged (`SAVE_VER` 35): a 1.6.0 save loads in 1.6.1 — saved
  by the 1.6.0 binary and restored by this one in ZEsarUX, on both machines.

### Fixed
- **Swapping places with your dog no longer hides what lies there.** Stepping
  onto the dog (or a shopkeeper, or a peaceful) swaps you, and that move used
  to end before the square was looked at: an item under the dog was never
  announced and gold under it was left on the floor. You now arrive as on any
  step — told what is there, gold picked up, an altar read, a shop's door
  greeted. On both machines; the swap has skipped it since the dog arrived.

## [1.6.0] — 2026-10-05

**Artifacts, tools, a weapon ladder — and four classes that each win their own
way.** Every addition plugs into something the game already did, every
calibration was measured with `tools/balance.py` before it was set, and every
change was proven in ZEsarUX on both machines.

### Added
- **Artifacts.** Offer at an altar of your own god, from experience level 3
  with your luck not below zero, and a pleased god may hand you one of five
  instead of the usual boon: **Sting**, **Sunsword**, **Trollsbane**,
  **Dragonbane** or **Stormbringer** — always of your alignment or of none,
  never one this game already holds. Each hunts something this dungeon holds,
  for double damage: Sting the orcs, Sunsword the zombie, the wraith and the
  vampire, Trollsbane the trolls, Dragonbane the dragon. Sting glows blue
  while an orc is near; with Trollsbane in your hand no troll knits its
  wounds; Stormbringer heals you by half of every blow, and it and Excalibur
  turn the wraith's and the vampire's drain. Acid never eats an artifact, and
  `w` always prefers one.
- **Tools**, a new class (`(`), applied with a new key, `a`:
  - the **skeleton key** opens a locked door for good — 70 + Dex in 100 a try;
  - the **pick-axe** digs: `>` down through the floor in five turns, any
    direction through rock or a wall in three — and **the level remembers the
    tunnel**, through the stairs and through a save. A monster coming into
    view stops the work, and `a` at the same spot carries on. Doors, shop
    walls and the Amulet's floor will not give;
  - the **magic whistle** brings your dog from anywhere on the level;
  - the **blindfold** keeps you blind while it is on: no floating eye catches
    your gaze, and with telepathy every monster on the level shows;
  - the **unicorn horn** cures confusion, poison and blindness — or, cursed,
    brings one on.

  A slain dwarf leaves a pick-axe one time in three; tools also lie on levels
  and sell in shops.
- **Each class plays its own way.** The Valkyrie stays the easy start; the
  other three each get NetHack's own edge:
  - the **Wizard**'s power comes back fastest, a point every eight turns, and
    force bolt hits for 2d12 — NetHack's dice — instead of 4–9;
  - the **Rogue** is born **stealthy** — a sleeper beside you stays asleep two
    turns in three — and **backstabs** a sleeping monster for extra damage
    that grows with experience;
  - the **Tourist** carries an **expensive camera**, 50 charges, reloaded by a
    scroll of charging: its flash blinds a monster for 5–14 turns, and blind,
    it wanders, cannot dodge, never strikes and never breathes fire.

  Modelled with the dog, over 3000 runs each, the four now win about 59%
  (Valkyrie), 43% (Wizard), 35% (Rogue) and 19% (Tourist) of their runs —
  NetHack's order. The Wizard, the Rogue and the Tourist were at 0–1%.
- **The weapons go on past the long sword**, NetHack's way: one-handed blades
  stay about a long sword however deep, and the extra damage is in the
  **two-handers** — the **battle-axe** from Dlvl 20, the **mattock** from 28 —
  paid for with the shield. `w` passes one over while you wear a shield and
  `W` a shield while you wield one, and each says why. The **silver saber**
  (Dlvl 24) burns vampires and imps for double.
- **New things to find**: the **amulet versus poison**, which every poisoning
  asks about, from the snake's bite to the murky fountain; the **potion of
  full healing** — every HP back, more maximum, the eyes cleared — rare and
  deep on purpose; the **scroll of scare monster**, under which no monster
  strikes you (read, it freezes everything near; picked up too often, it
  turns to dust); and the **scroll of gold detection**, which maps every pile
  of gold — or, confused, every hidden trap.
- **New flesh**: a **wraith's corpse** is an experience level, and a
  **leprechaun's** or a **nymph's** brings on teleportitis.

### Fixed
- **On the Next, every face was missing.** The tilemap treated pixel value
  15 as transparent, and that is the skin colour: the hero had a black band
  between helm and mail, and the shopkeeper had no head at all — nor did the
  vampire, the high priest, the gnome, the dwarf or the nymph. They are back.
- **Excalibur could be made twice**: a second long sword dipped in a fountain
  became a second Excalibur. It is unique now.

### Changed
- **Art pass.** Redrawn on the Next: the troll (brown, NetHack's colour, arms
  to its knees), the zombie, the kobold (snout and spear — it read as a
  torch), the bat (its legs made it a crab), armour (a mail shirt — the
  shield read as a tombstone) and the snake (it read as a boot). On the 128K:
  the zombie, the kobold, the snake (it read as an 8) and the mine hole, and
  new inks, so that four monsters are green instead of seven.
- `SAVE_VER` 35. **Saves from 1.5 will not load**; the game names the old
  save and asks before deleting it — answer `n` and it stays for the 1.5.0
  binary.
- `tools/balance.py` models the classes as they play — the force bolt, the
  stealth and the backstab, the camera — and calibrating them exposed two
  errors in the model itself, pulling opposite ways. One random stream
  served all 600 lives of a batch and wrapped its 65535-step period about
  33 times, so the lives were phases of one cycle; and every class got a
  free sneak attack in half its fights, where the game wakes a sleeper beside
  you unless you are stealthy. Each life has its own stream now, and only
  the stealthy strike first. Figures quoted for earlier versions came from
  the old model.
- On the 128K the inventory moved within Bank 5 to make room for more tiles,
  and the pick-axe's tunnels live in its free tail on both machines.
- The `zrcp-verify` skill gained a second emulator: a headless MAME harness
  that loads the real 128K tape through the 128 ROM and reads memory without
  stretching time.

## [1.5.0] — 2026-09-27

**A code audit's ten findings, fixed** — every one confirmed in the code
first, and every fix proven in ZEsarUX on both machines.

### Fixed
- **A trap door on Dlvl 50 dropped you into the Gnomish Mines.** The level
  below 50 is, inside the game, Mine:1 — one climb from Dlvl 2 with the Amulet
  in your pack and the whole ascent skipped; a trap door on Mine:4 made a
  Mine:5 that does not exist. Where there is no level below, a trap door is
  now a dart trap (NetHack swaps in a rock trap for the same reason).
- **Healing near 255 HP could leave you on 1**, and a fourth potion of
  blindness could cut the darkness short: HP and the status timers wrapped
  past 255 instead of stopping there. Every heal and every lengthened status
  now stops at the top.
- **Force Bolt and the sleep spell hit your dog and the shopkeeper**, which
  thrown weapons and wand bolts fly past. A wounded dog came back whole on the
  next level; a killed one came back from the dead, and counted as your kill.
  The spells fly past them now too.
- **A killing throw usually lost the weapon.** The corpse or loot fell on the
  very square the weapon was about to land on, and the weapon was already out
  of your pack. A thrown weapon now lands beside a taken square, and a floor
  already holding eight things gives up its oldest corpse to make room; only
  if even that fails does the weapon stay in your hand. The same goes for a
  nymph's loot when she dies on a doorway or you leave her level: it lands
  beside her, or else comes back to your pack, instead of vanishing.
- **Killing a wanderer, a summoned monster or a follower could mark one of
  the level's own monsters dead** for your next visit: the kill memory named a
  monster slot, not a monster. It only remembers the level's own now.
- **Monsters in the Big Room, the cavern and the maze could lose track of
  you.** The pathfinding flood stopped after 696 cells and left up to 578
  walkable cells with no way to you — a monster twenty squares off round a
  wall in the Big Room simply stood there. It now reaches every cell, and
  stops as soon as the last monster that will use it has its route. It is
  cheaper, too: on the 128K a turn that routes a monster round a wall in the
  Big Room took 0.9 s of pathfinding and now takes about 0.25 s; on the Next
  the difference stays below what can be measured.
- **Teleporting could put you inside rock or a wall** on the cavern, crypt
  and temple levels (their rooms are drawn around the rock), inside a Mines
  pillar, or on top of a monster when every try failed. Every random teleport
  now lands on open ground.
- **A save cut short or damaged on the card was loaded anyway** — and then
  deleted — and a full or write-protected card still said *Game saved*. A save
  is now sealed with its length and a checksum, written with every transfer
  checked, and read through once before anything of it is loaded; a bad one is
  named and left for you to keep or delete, like another version's save.
- **Saving and restoring rewound the dice**, and so did every trip up and down
  the stairs: generating a level reset the random stream to the same point
  each time. The stream is saved now, and generating a level leaves it alone.
- **A restore that failed after `S`** — the card pulled out after *You may
  switch off* — began a "new" game on the saved run's depth, with its gold and
  its kill memory. It starts clean now. And a new game after a death or a win
  no longer inherits the last run's spell-power regeneration.

### Changed
- `SAVE_VER` 30 (the dice and the checksum). **Saves from 1.4 will not load**;
  the game names the old save and asks before deleting it — answer `n` and it
  stays for the 1.4.x binary.
- Save & restore moved out of `nexthack.c` into its own `save.c`, which gave
  the fullest bank on the 128K 1.2 KB back. The pathfinding queue shrank from
  696 entries to a ring of 256, freeing 880 bytes of Bank 5 on both machines.
- `tools/balance.py`'s source references point at the moved lines again.
- **The build refuses a module the assembler had to mangle.** On the way this
  toolchain showed a trap: a pointer plus a negative constant can come out of
  z80asm with the wrong offset, and only a warning says so. Both build
  scripts now treat that warning as a failed compile. A scan of every module
  of both builds found no other case — nothing released was affected.

## [1.4.1] — 2026-09-25

**Left alone, the title plays itself** — and resting no longer undoes the
doors you kicked in.

### Added
- **An attract demo.** The title holds for its theme twice through (about 46
  seconds), then hands the screen to fifteen seconds of play: a hero of a
  random class, kit and all, walks with the dog through consecutive levels of
  a random dungeon — magic-mapped, so each level shows whole — down to the
  stairs and on to the next, now and then in the Gnomish Mines, under a
  blinking *DEMO - press any key to begin*. Then the title again. Any key, on
  the title or in the demo, begins the game.
- The demo is the game's own generator and renderer, but nobody in it fights,
  picks anything up or dies, so nothing it shows reaches your game: a new run,
  and a save restored after the demo has played over it, start exactly as they
  did before. It also reads the ROM's key latch, so a quick tap is caught even
  while a level is being built.

### Fixed
- **Resting could re-lock every door you had forced.** When a monster came
  into view and ended an `R` rest, the game forgot every locked door you had
  kicked open or unlocked with a wand of opening — on every level, not just
  that one. One misplaced line from 1.3's locked doors, meant for the new-game
  reset, which in turn never had it: **a new game after a death inherited the
  last run's forced doors**, in a world where they named different doors. Both
  fixed.

### Changed
- `tools/balance.py` and the notes beside it described a game with no rest
  command; they now price `R` as it is: it saves keypresses, not HP, and each
  wanderer it attracts is one ordinary fight, so resting pays through Dlvl 25
  for every class and stops paying by 30–40.

Save-compatible with 1.4.0.

## [1.4.0] — 2026-09-22

**Seventeen new things to find, and a reason not to trust them.** Rings and
wands go unidentified; seven rings, three wands, four scrolls, two potions and
a carrot join the catalogue — each one plugged into something the game
already had, because in NetHack the interactions are the content.

### Added
- **Rings and wands are unidentified**, like potions and scrolls — "onyx
  ring", "iron wand (5)". The looks are a real per-game shuffle now: the old
  rotation gave a whole class away the moment one look was learned, since the
  pool order was fixed. Each pool holds more looks than types, so the last
  unknown one cannot be named by elimination. You learn a type by using it,
  by watching it work, from a scroll of identify — or from what a shop asks.
- **Seven rings**, each an effect rather than armour:
  - **slow digestion** — food lasts sixteen times as long;
  - **free action** — the floating eye's gaze no longer freezes you;
  - **teleport control** — every teleport asks where, on the farlook cursor:
    the scroll, the trap, the spell and the ring all go through one door now;
  - **stealth** — your footsteps wake no sleeper, and one beside you only
    stirs one turn in three;
  - **hunger**, **aggravate monster** and **teleportitis** — the junk. As in
    NetHack they are cursed nine times in ten, so an unknown ring you try on
    may be one you cannot take off.
- **Three wands.** **Opening** unlocks the first locked door in its line —
  the second answer to a locked door, after the boot. **Fire** and **magic
  missile** are rays through the whole line. A wand names itself when you see
  it work; a bolt into an empty room keeps its secret.
- **Four scrolls.** **Genocide**: name a monster by its letter and the dungeon
  never sends another — your dog goes with its kind, as in NetHack; cursed, it
  sends a pack of them to you instead. **Charging** tops up a wand, or with no
  wand, your power. **Destroy armor** takes the outermost piece, cloak first,
  which makes a cloak armour for your armour. **Amnesia** takes this level's
  map and a third of what you had identified.
- **Two potions and a carrot.** **Gain ability** raises an attribute (blessed:
  all six). **Gain energy** grows your power. The **carrot** cures blindness.
- **Scrolls can be blessed or cursed**, one in eight each, and it matters to
  four of them: cursed enchantment lowers instead of raising (negative
  enchantment shows now — "-1 chain mail"), cursed remove curse fails, cursed
  charging drains, cursed genocide summons.

### Changed
- **`P` asks which ring or amulet to put on**, through the same menu as
  `q`/`r`/`z`, silent when you carry one kind. It used to choose the best ring
  itself — with unknown looks that would be an oracle. A cancel costs no turn.
- **Only the ring of protection adds to AC.** Every ring used to add its
  blessing, so a blessed ring of regeneration was +1 AC.
- **Your starting kit is identified**, as in NetHack — the Wizard's potion no
  longer reads "ruby".
- **Generation weights.** Each type now has a weight within its class, where
  before every type was equally likely and each new one silently diluted the
  rest. Weapons and armour keep equal weights and resolve exactly as before.
  The potions are weighted so that healing keeps its share: at equal weights
  the two new potions would have cut it from a third to a quarter, and
  `tools/balance.py` measured that alone halving the Valkyrie's wins (22% to
  11% with the dog). With the weights the difficulty curve is 1.3's to within
  a floor. The seven new rings dilute protection to a ninth of rings, which
  the same measurement shows costs nothing.
- **Discoveries (`D`)** list rings and wands too, and page when the list
  outgrows the screen.
- `SAVE_VER` 29. **Saves from 1.3 will not load**: the catalogue passed 48
  types, which widens the identification bitmap, and the genocide mask joined
  the file. The game names the old save and asks before deleting it — answer
  `n` and it stays for the 1.3 binary.
- A fresh inventory screenshot in the README carries the release: a ring of
  teleport control on the hand beside a cursed ring and a wand that are still
  only their looks, a wand of opening, a scroll of genocide and a carrot.

### Fixed
- **Putting on an amulet took two turns.** The dispatcher charged one and the
  amulet path a second.
- The discoveries header no longer lingers on the Next's message line after
  you close the screen.
- **A bug-hunting pass before release** found eight more, each proven in the
  emulator before it was fixed and after:
  - **A cursed ring, suit or weapon came off with `d`.** `P` said *stuck
    fast* and `d` dropped the same ring a keypress later — or sold it in a
    shop — and `t` threw a welded weapon. Every curse since blessed/cursed
    gear arrived was a formality, and the junk rings this release curses nine
    times in ten would have been free samples. A cursed piece you have on now
    stays on until a remove curse, a prayer, an altar's offering or a scroll
    of destroy armor lets go; the refusal costs no turn.
  - **128K: a full pack did not fit the screen.** The inventory and the drop
    list stopped at the 23rd item, so the last three were in the pack and on
    no screen; an item menu did not stop at all, and a long one drew past the
    bottom of the screen into the printer buffer and the system variables
    (430 bytes, measured). Lists page with `--More--` now.
  - **Next: a long drop or item menu left its tail on screen** below the
    status bar, over the help pointer, for the rest of the game.
  - **A monster's loot was never cursed.** Junk rings dropped by a kill came
    off as easily as they went on; rings and scrolls from kills now follow the
    same curse rules as the ones on the floor.
  - **A potion of gain energy could lower your maximum power** — from the 61
    an altar's boon can leave it at, down to its own cap of 60.
  - **A genocided nymph kept what she had stolen.** It now drops where she
    stood, as when she is killed.
  - **A cursed scroll of genocide read in a shop** put the horde inside it,
    where the monster AI never lets anything move — a pack that could bite
    you and never leave. It stays out of shops now, like the wand of
    teleportation already did.
  - **Killing a floating eye still froze you.** Its gaze needs it alive, as in
    NetHack; the freeze also buried the kill message.

  `tools/balance.py`'s source references — the `file:line` beside each
  formula — had drifted: 37 of its 40 pointed at the wrong line. Re-anchored.
  None of the fixes touches anything the model draws, so its numbers stand.

### Not done
- **Restore ability** was planned beside gain ability and left out: nothing
  in this game ever lowers an attribute, so it would restore nothing and ship
  as a dud. It waits for a drain to answer — NetHack's poison can sap
  Strength.
- **One ring at a time.** NetHack has two hands, and teleportitis plus
  teleport control is its classic pair; here the hand holds one ring, so that
  pair cannot be worn. Two hands need a remove command and a which-hand
  prompt — their own change.

## [1.3.1] — 2026-09-03

### Added
- **An incompatible save is now named, and you decide.** A save from another
  version used to be deleted without a word: you started the game, saw a fresh
  dungeon, and only then worked out that a run was gone. The game now says what
  the file is and asks. Answer **n** and it is left alone — if you still have
  the older binary, your save is still there for it.

### Changed
- Fresh README screenshots. The inventory one carries the release: a full
  armour set worn piece by piece, an amulet round the neck, and `AC:1`.

### Fixed
- **A worn amulet is now labelled `(on neck)` in the inventory.** Wielded
  weapons, worn armour and rings on the hand all say so; amulets fell through
  to no label at all, because before 1.3 the only `"` in the game was the
  Amulet of Yendor, which is never worn. Same shape as the rest of that
  batch's defects: a condition that was right until the world grew.

## [1.3.0] — 2026-09-02

**Four steps toward NetHack, and three passes of looking for what they broke.**
Armour is worn as a set, doors lock, amulets hang round your neck, and the
Oracle keeps her fountains. Eleven defects were found before release; the two
worst were invisible to tests that had already passed.


Four steps toward NetHack, and one that measurement stopped at the door.

### Added
- **Armour is worn as a set.** Shield, helmet, boots and cloak join the body
  suit, each in its own slot, and `W` adds the piece that improves the set
  most — press it again and the next one goes on. `recompute_gear` had always
  summed every worn `[` piece; a single `unworn_class('[')` in `do_wear` was
  all that kept the hero in one suit.

  It is also the largest balance lever measured so far, which is why it was
  measured first. Five slots each carrying a depth-scaled enchantment reach
  `armor_def` 17–19 against a single suit's 10 — 30% to 99% of runs won,
  against 1.4–9.6%. So the total **saturates at 10** (`ARMOR_CAP`), which is
  NetHack's diminishing returns done cheaply: the displayed **AC keeps
  falling** with every piece, so a new find still reads as progress even after
  the reduction has topped out. With the cap, the curve is 1.2's to within a
  point.

- **Locked doors, and a boot to open them.** About one door in four below
  Dlvl 3 is locked — a pure side hash, like traps, so the deterministic
  generation stays byte-identical. Walk into one and it refuses; `K` kicks it,
  and Strength decides. Forcing is remembered per depth, so a door you break
  stays broken.

  Monsters and the pet still walk through. That is deliberate: routing the BFS
  around a locked door would cork the dog behind the first one you kick past,
  which is the left-behind bug that took three separate fixes to kill. A locked
  door costs you turns; it does not fortify you.

- **Two amulets you can wear.** The amulet of **ESP** grants telepathy while
  it is on — the floating eye's gift, without eating one. The amulet of **life
  saving** spends itself instead of you, checked in the one place the turn loop
  already looks at `dead`, so every way of dying is covered. `"` on the floor
  is now one of these anywhere except the bottom, where it is still the Amulet
  of Yendor.

- **The Oracle.** A sixth hand-drawn level: Delphi's central chamber with
  fountains at its corners and the seat in the middle, four outer chambers,
  and the stairs at opposite ends so the level has to be crossed. It cost
  156 bytes — the LZ packing put six templates in 1020 B.

### Changed
- `SAVE_VER` 28. **Saves from 1.2 and earlier will not load.** The armour
  slots pushed the object catalogue past 40 types, which widens the
  identification bitmap, and the forced-door mask joined the file. 27 had been
  the format since the 1.0 freeze.

### Fixed
Found by auditing this batch before release, not by playing it — and the two
worst were invisible to the tests that had already passed, because those poked
items straight into the pack instead of picking them up off the floor.

- **An ordinary amulet handed over the victory.** Picking up any `"` set
  `has_amulet`, so an amulet of ESP found on Dlvl 6 won the game on the way
  back up. Only the real Amulet does that now.
- **The new amulets were never generated.** They existed in the catalogue and
  nowhere in the dungeon — no loot block placed a `"`. They now turn up rarely
  from Dlvl 6. This is what hid the bug above.
- **A scroll of enchant armour always hit the same piece.** With one suit,
  "the first worn `[` in the pack" was "the only one"; with a set it silently
  became "the lowest inventory slot", so every `+1` went into one item — which,
  once it reached `ARMOR_CAP`, did nothing at all while the other four stayed
  at +0. Acid corroded the same piece for ever for the same reason. Both now
  go through one `pick_worn()`, which is the fix for the underlying fault:
  the "which worn piece" question had three separate answers.
- **Locked doors could appear on a shop.** The keeper cannot leave to let you
  in. Same rectangle test traps already use.
- The `"` on the floor no longer announces itself as the Amulet of Yendor
  anywhere but the bottom.

- **A second audit pass, prompted by asking what else the mines distort.**
  Inside the branch `dlvl` runs 51-54 while the level *plays* as `eff_depth()`
  3-6, and four of these came from reading the wrong one:

  - **Carrying the Amulet of Yendor stopped you wearing any amulet.** Every
    amulet has `prop 0`, so `find_best` returned whichever sat first in the
    pack; with Yendor there it failed the "not Yendor" guard and put nothing
    on — during the climb out, which is exactly when life saving matters.
  - **A cursed amulet came off for free**, where a cursed ring is *stuck fast*
    and cursed armour is *welded on*.
  - **The loot could put a second `"` on Dlvl 50**, where any `"` resolves to
    the real Amulet — two of them on the win level.
  - **The mines minted a false Amulet.** The loot gated on `dlvl >= 6`, but
    the item is *resolved* at `eff_depth()`; below 6 no amulet is eligible and
    `resolve_otyp`'s empty-pool fallback returns the first `"` in the
    catalogue, which is Yendor's own. Gated on `eff_depth` now, and
    `resolve_floor` refuses to mint an Amulet off the win level whatever the
    caller does.
  - **The wand of digging never worked in the mines.** `dlvl >= DLVL_AMULET`
    reads as "the Amulet's floor", but 51-54 are all >= 50, so the whole
    branch refused to dig — and spent the charge doing it. It now refuses only
    where there is no floor below, and a refused dig keeps its charge.

  Nine further comparisons decided loot or difficulty from raw `dlvl`. All of
  them happen to give the same answer today, which is luck rather than design,
  so they now read `eff_depth()` too — no generated level changes. The rule is
  written down in `game.h` beside the mines' own constants.

- **The nymph could steal the Amulet of Yendor.** Dropping it and selling it
  were both barred, with the same comment — *never lose the win item* — and
  the third route out stayed open. She blinks across the level with what she
  takes and the level forgets her when you leave, while `has_amulet` stays
  set. Pre-existing, found while auditing the shop path.

### Not done
- **Tools (`(`)** — a pick-axe and a lock pick were planned and are blocked,
  not deferred. A new item class needs a new tile, and the 128K's `udg_bitmap`
  ends *exactly* at `inv[]`: `NTILES` is 48 and a 49th tile corrupts the
  inventory. There are 192 free bytes below `SSHADOW` to move the bitmap into,
  which would buy 24 tiles, but relocating a Bank-5 tenant is its own change
  with its own emulator pass — not something to bundle here.

## [1.2.0] — 2026-09-01

**The cliff becomes a climb.** Armour stopped improving on Dlvl 10 while the
dungeon kept scaling for forty more floors; this is the repair, measured on
both sides of the fault.


### Changed
- **Armour subtracts instead of cancelling.** A blow your armour covered used
  to be a clean miss; now it grazes you for 1 — "You shrug off the rat!" This
  is the repair the *armour cliff* actually needed. The old rule was
  all-or-nothing at both ends of the dungeon: it made the first twenty floors
  harmless (86% of blows simply bounced on Dlvl 1) and had nothing left to
  give on the last twenty, where no armour you can find covers a bite of
  13–20. Measured, a fight now costs 0.4% of your HP bar on Dlvl 3 instead of
  0.1%, and 2.1% instead of 0.4% on Dlvl 10, while every depth past 20 is
  untouched.

### Added
- **Three deeper armours** — splint mail (Dlvl 14), banded mail (Dlvl 20) and
  dragon scale (Dlvl 28). The catalogue used to stop at plate mail on Dlvl 10
  while the dungeon kept scaling for forty more floors, so the second half of
  a run was fought in first-half armour. These continue the cadence the first
  four set rather than inventing a new one.

  Together the two changes turn the curve from a plateau and a cliff into a
  climb: a fight costs 0.4% of your bar at the top, 2% through the middle
  floors and 9.2% at the bottom, rising the whole way. Runs reach Dlvl 42–47
  instead of 38–48, and the Amulet is now genuinely winnable — a few percent
  of runs, more if you use the rest command well.

  This is the second and last change out of `tools/balance.py`'s six
  candidates. The two shipped separately on purpose, and the measurement says
  why: rest and deep armour *multiply* (0.9% and 2.4% alone, 37% together),
  while the soak rule and the ladder *compensate* — the rule hardens the game
  where the ladder softens it.

### Fixed
- Nothing: this release changes balance only. Saves from 1.1 still load
  (`SAVE_VER` 27; the three appended types leave the save's size unchanged).

## [1.1.0] — 2026-08-30

**Rest.** The first change chosen by measurement instead of instinct: of six
candidate repairs to the difficulty curve, this is the only one that makes a
run longer without making a fight different.

### Added
- **Rest (`R`).** Sit still and let your wounds close, instead of tapping
  `s` a thousand times. The turn loop simply keeps spending turns without
  reading a key — the same path a sleeping hero already used — so upkeep,
  the monsters' turn and the redraw all behave exactly as they do when you
  play them out one keypress at a time.

  It stops for good reasons only: you are healed, something hostile and
  awake comes into view, you press a key, or hunger reaches *weak*. A
  sleeping monster does not interrupt it, and neither does a shopkeeper, a
  peaceful townsman, a hidden mimic or the floating eye — none of them are
  coming for you. Everything else ends the rest **before** the turn is
  charged, so you never take a free hit for having rested one turn too long.

  This is the first change to come out of the balance instrument
  (`tools/balance.py`). It was chosen over five other candidates because it
  is the only one that lengthens a run without touching the shape of combat:
  measured across 1600 simulated lives, absorption and HP-per-fight at every
  depth are unchanged, while the median death moves from Dlvl 32-41 to
  38-48 and the win rate goes from zero to about 1%. It does not fix the
  underlying armour cliff — that is a separate decision, deliberately not
  bundled here, because the model says two such changes at once take the
  game from unwinnable to trivial.

### Fixed
- The `?` screen's character sheet no longer overprints itself on the 128K.
  All six attributes on one line came to 33 columns from column 2, so on a
  32-column ULA the last one wrapped and landed on top of the alignment
  underneath. They now sit three to a row, with the alignment moved up beside
  the class name — the same number of rows as before, and unchanged in
  substance on the Next's 80 columns.

### Changed
- The map is not redrawn between resting turns. Nothing on it can change
  in a way you may act on — anything that appears ends the rest first — and
  the Next redraws all 80x32 cells every turn, which held resting to about
  six turns a second. Skipping it doubles that.

## [1.0.0] — 2026-07-31

**NextHack is finished.** The dungeon is whole: fifty levels, the Gnomish
Mines, the Amulet at the bottom and Moloch's retinue waiting on the climb
back out — on a ZX Spectrum Next and on a plain 128K, from one source tree.

### Added
- **A title theme.** The AY chip plays NextHack in while you look at the
  loading screen — a heroic sixteen bars in D Mixolydian over a walking
  bass, an arpeggiated chord and a kick-and-snare backbeat. The 128K and
  the Next both have the chip; a machine without one simply shows the
  title in silence.

### Changed
- **New title art**, drawn at final resolution without the Spectrum colour
  stripes, which never sat well once the 128K squeezed them through its
  two-colours-per-cell screen.
- Fresh screenshots throughout the README.

### Fixed
- **No more altars (or fountains) on a shop's floor.** They could be
  dropped into the middle of a shopkeeper's stock — and there the altar
  was useless as well as out of place, since dropping an item in a shop
  sells it, so nothing could ever be sacrificed or blessed on it. Both
  now keep out of shops; a level whose altar would have landed there
  simply has none.
- **The wand of teleportation stopped being an off switch.** It could
  drop its victim inside a shop, where a monster is stranded for good —
  the dungeon's inhabitants keep out of shops, so once every step leads
  into one it can never move again. A dragon could be retired with a
  single charge. It could also land the monster on top of the shopkeeper
  or another creature. The bolt now looks for a clear spot, as the
  nymph's blink always did, and the victim merely shudders if there is
  none.
- **Teleporting no longer lands you on top of a monster** (scroll or
  trap): you would share its square, and had to step away and back to
  fight it.

### Note — the save freeze holds
- Saves from 0.10.0 onward **load unchanged**.

## [0.12.2] — 2026-07-31

A pre-1.0 code audit, and the ambushes it found were not happening.

### Fixed
- **Mimics ambush again on the 128K.** A disguised mimic standing right
  beside you neither revealed itself nor bit, for as long as no *other*
  enemy was awake nearby — and since 0.11 spawns half the dungeon asleep,
  that was the common case. It now springs the moment you come alongside,
  as it always did on the Next.
- **Your max HP can no longer collapse on a level-up.** A hero who had
  built up to around 250 max HP — reachable through repeated altar
  boons — wrapped an internal counter on the next level gained, and came
  out of it with **2**. Now capped.
- **A rich winning run gets the score it earned.** A hoard near the gold
  ceiling overflowed the score on the very screen that celebrates the
  run, showing a tiny number; gold picked up from the floor or scooped
  from a fountain could overflow the purse itself. Both now hold at
  their maximum.

### Changed
- The 128K build no longer emits `nexthack128.sna`. It was never usable —
  a snapshot cannot carry the code-banked RAM banks, so it booted the
  title and crashed on the first banked call. The `.tap` is the artifact.
- **Wider hardware reach on the 128K.** The tape loader read a system
  variable that only exists on a real 128K ROM; on a 48K machine with a
  RAM-expansion interface that address is a scratch buffer full of
  garbage, and one stray bit in it would have locked the memory paging
  before the game finished loading. It now takes only the bit it needs.
  (The game itself never used the shadow screen, which is the one thing
  those interfaces cannot do.)

### Note — the save freeze holds
- Saves from 0.10.0 onward **load unchanged**.

## [0.12.1] — 2026-07-13

Stones by hand, and the dog learns to excuse itself.

### Fixed
- **The dog no longer gets corked behind townsfolk.** The pet only knew
  how to displace the shopkeeper, so a peaceful gnome idling in a
  one-wide room trapped it while you walked on alone. It now swaps past
  peaceful monsters exactly as you do.

### Changed
- **The mine walls are real cobbles now.** Rebuilt from a mock drawn by
  the player, over three markup rounds: separate rounded stones at
  varied offsets, none touching, meandering joints — the old two-band
  tile read as brick courses in a run. On the Next the joints went
  black like the surrounding rock (mine rooms stop looking like filled
  boxes) with a tan fleck inside each big stone; the 128K keeps its
  approved pure two-tone.
- **The 128K acid blob has eyes**, matching the Next's.

### Note — the save freeze holds
- Saves from 0.10.0 onward **load unchanged**: art and pet manners only.

## [0.12.0] — 2026-07-13

The interface release: the dungeon answers your questions — and everyone
walks like they mean it.

### Added
- **Farlook (`;`).** Move a yellow cursor around the map and be told what
  lies under it — monsters (with their mood: *"A sleeping orc."*, *"A
  peaceful dwarf."*), items (resolved exactly as pickup would find them),
  altars with their alignment, terrain. Honours what you can actually know:
  unexplored cells stay unknown, monsters show only in sight (or by
  telepathy), remembered terrain reads from memory. Costs no turn.
- **Creatures face where they walk.** The hero, the dog and the rat turn
  left or right with their last horizontal step (you also turn toward a
  wall you bump). On the Next this is the tilemap's hardware mirror; on
  the 128K three pre-mirrored shapes are built at startup. The hero's
  sword leads the way — his art is right-handed, so he mirrors walking
  left; the animals face left and mirror walking right.
- **Wanderers no longer pop into view.** A wandering monster could
  materialise on a cell you were looking at — across a lit room it read
  as a ghost (a wild dog doubly so). They now always arrive off-screen,
  as the code always claimed they did.
- **Discoveries (`D`).** A screen mapping each potion and scroll look you
  have identified to its true meaning — *"ruby potion: healing"*, *"ZELGO
  MER: remove curse"*. What you haven't drunk or read stays a mystery.
  (NetHack's `\` also works, but no Spectrum key produces that character,
  so `D` is the listed key.)
- **Two new traps.** The dungeon's floor now also hides teleport traps
  (whisked across the level) and rust traps (a gush of water that rusts
  your worn armour), alongside the trap door, the dart and the gas.
- **Combat speaks with more than one verb.** You hit, smite and whack;
  what bites you may also hit or tear at you. Kills are killed, slain or
  destroyed — murder is still murder.

### Fixed
- **The dog no longer misses a cramped landing.** Arriving on stairs whose
  every neighbouring cell was walled off or already occupied (a tiny stairs
  room with a monster on its one free tile) made the dog sit the level out.
  It now settles for a spot two cells away and trots back to your heel.
- **128K: the fog of war of parked levels could rot.** When the explored-map
  pool grew to 12 levels (0.10.0), the renderer's repaint scratch was left
  sitting inside it, silently corrupting two stored levels' memories. The
  scratch moved to a genuinely free spot — revisited levels now always
  remember exactly what you explored.

### Changed
- The rat gained an eye under its ear on both targets — a red pixel on
  the Next, a one-pixel hole in the 128K silhouette — and the Next dog's
  eye is now a white glint instead of a hole.
- The farlook cursor on the Next settled at a ~100 ms step after play
  found ~80 ms rushed and ~140 ms sluggish (it originally inherited the
  Next's brisk walk repeat and overshot).

### Note — the save freeze holds
- Saves from 0.10.0 and 0.11.0 **load unchanged**. Facing is a transient
  pose, trap types are derived from the standing side hash, and the
  discoveries screen only reads what the pack already recorded: the save
  format and the deterministic generation streams are untouched.

## [0.11.0] — 2026-07-12

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
