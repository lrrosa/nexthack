# NextHack — static code audit handoff

**Audited commit:** `c5e1053a448ed18deb18a808a8ebb0b246cdb6f9`  
**Date:** 2026-09-27  
**Targets:** ZX Spectrum Next + ZX Spectrum 128K

> Static audit plus targeted algorithmic simulation. Any code fix still needs both-target build and ZEsarUX/ZRCP verification per `AGENTS.md`, `CLAUDE.md` and `.claude/skills/zrcp-verify/SKILL.md`.

## Confirmed issues

| # | Finding | Main correction direction |
|---|---|---|
| #1 | Trapdoors can leave valid level IDs | Bound `IN_MINES()` and centralize legal descent |
| #2 | Healing can wrap `uint8_t` HP | Saturating heal helper using a wider temporary |
| #3 | Save/load ignores short I/O and malformed structure | Transactional save, checked I/O, validate-before-commit |
| #4 | BFS queue truncates chase maps | Redesign frontier/fallback; do not blindly grow Bank-5 queue |
| #5 | Random teleport can choose invalid/rejected cells | Shared valid-destination predicate + deterministic fallback |
| #6 | Runtime RNG state is not restored | Save RNG state and restore it after deterministic `build_level()` |
| #7 | Transient monster slots collide with `mon_dead` identity | Separate runtime slot from persistent spawn identity |
| #8 | Force Bolt desynchronizes/resurrects pet | Consistent friendly-fire policy or pet-aware damage path |
| #9 | Additive status timers wrap modulo 256 | Saturating timer helper |
| #10 | Player-owned items can disappear on failed `floor_drop()` | Atomic ownership transfer with placement fallback |

Canonical discussions and full reproduction details are in GitHub issues #1 through #10.

## Key evidence

### #1 Level namespace
`game.h` currently defines `IN_MINES(d)` as only `d >= MINES_BASE`; valid mine IDs are 51..54. `spring_trap()` blindly increments `dlvl`. Trapdoor at Dlvl 50 therefore enters internal 51 (Mine:1), and trapdoor at Mine:4 can create 55+. Wand of Digging already contains the intended bottom guards.

### #2 HP overflow
`php`/`pmaxhp` are `uint8_t`; fountain, potion and spell healing narrow the sum before clamping. Example: 250 + 7 -> 257 -> uint8_t 1, so healing can reduce HP to 1.

### #3 Save integrity
`file_read()`/`file_write()` discard esxDOS transfer results. The canonical save is opened with truncation before replacement is known good. Restore assumes every block was read, then deletes the file. `item_load()` trusts `inv_count` and `otyp`, allowing malformed saves to drive out-of-range traversal/lookups.

### #4 BFS truncation
`BFSQ_SIZE` is 696 and Bank-5 space ends exactly at 0x8000. Exact-map simulation with the current BFS found: Big Room 1292 walkable cells with roughly 561–578 left `UNREACH`; `cavern` 1141 with 409–430 left `UNREACH`; `maze` 808 with 101–110 left `UNREACH`. Next comments describe chase as unbounded, so this is a direct behavior mismatch.

### #5 Teleport validity
`level_random_floor()` chooses inside room rectangles but never checks standable terrain, and after 12 rejected rolls returns the last rejected coordinate. Using the exact `txt2template.py` parsing rules, non-walkable random-room candidates exist in `cavern` (185/1292, 14.3%), `crypt` (52/440, 11.8%) and `temple` (24/532, 4.5%).

### #6 RNG save state
The save stores `world_seed` but not private `rng`. After load, `build_level()` -> `gen_level()` -> `rng_set(level_seed(dlvl))`, rewinding runtime randomness. Restore saved RNG only after deterministic level regeneration.

### #7 Monster persistence identity
`MAXMON=10` comments imply slots 0..7 tracked and 8..9 keeper/pet, but keeper/pet are appended at current `mcount`, and wanderers/summons/followers can reuse low dead slots. Generic kill code records `mon_dead` by `mi`, so a transient actor can poison a future deterministic spawn's persistence bit.

### #8 Pet state
`spell_ray()` sends Force Bolt through generic `hit_monster()` without excluding `pet_idx`. Generic damage changes `m_hp[]`/`m_alive[]` but not `pet_hp`/`have_pet`/`pet_idx`; normal pet death does. Nonlethal damage can disappear on stairs, and lethal damage can allow later pet recreation.

### #9 Status overflow
`st_conf`, `st_blind`, `st_sleep`, `st_poison` are `uint8_t` and many effects extend them with direct addition. Example: blindness 250 + 30 -> 24. Reapplying an effect can make it end sooner.

### #10 Item ownership loss
`do_throw()` removes a weapon, then ignores failure from `item_floor_drop()`. A killing hit can place corpse/loot on the same square first, causing the weapon drop to fail. `drop_held_1()` clears a nymph's `held_has[]` before its unchecked `floor_drop()`, so stolen player items can also vanish.

## Minor confirmed observation

`pw_timer` is not reset by `new_game()` while `heal_timer` is. A new adventure after death/victory inherits the previous spell-power regeneration phase. Low severity; not opened separately.

## Investigated and discarded candidates

- Normal stairs do not descend past Dlvl 50: the down-stair cell is replaced by the Amulet (and by the luckstone at Mine bottom). The confirmed bad path is trapdoor.
- `floor_reset()` does not store the wrong level after `dlvl` changes: stash identity is carried by `stash_prev` and updated by `floor_restore()`.
- `floor_item_desc()` does not return an unmapped const-bank pointer: `obj_desc()` uses a static writable buffer, therefore resident DATA/BSS.
- `class_name()` cross-bank use is safe under the current colocate groups in `banks.json`.
- FOV bitmap pointers target resident/fixed data, not swappable const sections.
- 128K `BANKM` looked suspicious for 48K+RAM interfaces, but `mktap128.py` explicitly initializes and maintains address 23388 as the loader's own paging shadow before game entry.
- xorshift zero-state is guarded: zero seeds become `0xACE1`.
- Amulet of Life Saving is correctly checked before normal death processing.
- Inspected directional ray paths have appropriate bounds protection.

## Suggested implementation order

1. #1 level identity.
2. #2 + #9 saturating arithmetic helpers.
3. #10 + #8 ownership/pet invariants.
4. #7 persistent monster identity.
5. #3 save robustness together with #6 RNG persistence (single save-version bump if possible).
6. #5 teleport validity.
7. #4 BFS redesign after measuring Bank-5 and performance constraints.

## Invariants worth encoding

- Valid `dlvl`: 1..50 or 51..54 only.
- `inv_count <= MAXINV` and every `otyp < NUMOBJ` before catalogue lookup.
- `floor_n <= MAXFLOOR`; stash counts and all floor coordinates valid.
- Runtime monster slot != persistent spawn identity.
- If a live pet is placed: `pet_hp == m_hp[pet_idx]`, type is dog, and death clears all representations atomically.
- Any bounded `uint8_t` additive resource/status update uses wider arithmetic or saturation.
- Never delete/consume a save until all expected bytes were read and full validation succeeded.

## Verification after fixes

Run repository checks and both target builds. Always run `python tools/bankmap.py next --check` and `python tools/bankmap.py zx128 --check` after changes affecting memory.

Use `.claude/skills/zrcp-verify` for behavior. Minimum scenarios:

1. trapdoor at Dlvl 50 and Mine:4;
2. healing and status timers deliberately set near 250;
3. Big Room/cavern/maze monster chase across the old BFS frontier;
4. repeated teleport in cavern/crypt/temple;
5. Force Bolt through the dog, both nonlethal and lethal according to chosen policy;
6. killing throw with corpse/death-drop collision and full floor pool;
7. nymph-held item during death/level change/save;
8. save/load RNG continuity against an uninterrupted run;
9. truncated/corrupt save at every major block boundary;
10. transient monsters reusing low slots followed by a level revisit.

## Guidance for the next AI

- Re-fetch `main` first. If its SHA differs from the audited commit, revalidate the relevant issue before patching.
- Prefer small independent code PRs instead of one mega-fix; bank-size changes can make unrelated patches interact.
- For #4, do not increase `BFSQ_SIZE` without a memory-map redesign.
- For #3/#6, coordinate `SAVE_VER`.
- For #7/#8, define actor identity/state ownership before patching individual call sites.
- Report both-target ZRCP results and `bankmap.py` results on every implementation PR.
