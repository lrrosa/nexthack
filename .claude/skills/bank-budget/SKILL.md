---
name: bank-budget
description: Check and manage NextHack's memory budget before growing the game — the resident half, the 16 KB code banks (Next PAGE_20/22/26/28, 128K BANK_0/1/3/4/6) and the shared Bank-5 data map. Use when adding a feature, module, tile, table or message strings; when a build reports a bank overflow or the resident half nears the stack floor; when changing a Bank-5 tenant's size (NTILES, FOV_SLOTS, MAXINV); or when something works on one target and crashes/corrupts on the other.
---

# Memory budget: check before you grow

NextHack is **code-banked** and the banks are near full. Growth fails in three
ways that all look like something else, so **measure first**:

```bash
python tools/bankmap.py
```

Prints, per target: the resident half vs the stack floor, every code bank's
free tail, and the **Bank-5 tenant map with overlap detection**. `--check`
exits 1 on any problem (usable as a build guard). Sizes are derived from the
sources (`NTILES`, `FOV_SLOTS`, `MAXINV`, `MAPW/H`, `BFSQ_SIZE`), so the map
follows the code instead of drifting from it.

Run it **before** starting, then again after the build.

## The three budgets, and which one you are spending

| Adding | Spends | Room today |
|---|---|---|
| Cold code, `__banked` entry points | a **code bank** | Next: PAGE_20 roomy, PAGE_22/26 tight; 128K: BANK_1 roomy, **BANK_3 and BANK_6 effectively full** |
| `static` data, arrays | the **resident half** | ~1 KB (Next) / ~3 KB (128K) to the stack floor |
| **String literals** (`msg()`, screens) | the **resident half** — unless const-banked | the classic silent sink |
| A Bank-5 array, tiles, the fov pool | the **Bank-5 map** | Next: **full**; 128K: a few small gaps |

Banked code's `static` data and string literals stay **resident** regardless of
which bank the code lives in. A code-bank reclaim does not help resident
pressure, and vice versa.

## When a code bank is full

1. **Do not shrink the feature first.** Relocate a module: move a whole,
   self-contained `.c` to a roomier bank (`#pragma codeseg`), not part of one —
   in-file `#pragma codeseg` does NOT partition by position.
2. **A module is safe to move only if** every cross-module call it makes is
   `__banked` or resident (a direct intra-bank call crashes once it is
   cross-bank), and its `constseg` tables are consumed **only** while its own
   bank is mapped.
3. **Coupling traps** (these block the obvious moves):
   - `classes.c` / `spells.c` consts are pointer-coupled to consumers in
     `nexthack.c`'s bank — moving them means moving the consumers too.
   - PAGE_22 (bank 11) must hold **code + const under 16 KB**: `show_layer2`
     reads `title_pal`/`victory_pal` in place. If they spill past `$170000`
     the title colours scramble — visual only, invisible to ZRCP. `bankmap.py`
     checks this.
   - Next bank 12 already holds PAGE_22's const spill: a `PAGE_24` section
     there collides. Use PAGE_26/28.
   - 128K: a new bank must be added to `tools/mktap128.py`'s `BANKS` list, or
     it is simply absent and the first banked call crashes.
4. Precedents that worked: `monster_ai.c` → PAGE_26/BANK_6; `item.c` + all its
   consts → PAGE_28/BANK_0 (~3.2 KB resident reclaimed); `monster_spawn.c`
   split out of a full AI bank; `sfx.c` bounced BANK_3 → BANK_1.

## When the resident half is tight

In order of preference:
1. **const-bank the strings**: `#pragma constseg <the module's own bank>` puts
   its literals in that bank. Safe because they are consumed while it is
   mapped. **Never pass a constseg'd literal into another bank's `__banked`
   function.**
2. **const-bank a read-once table** (`gfx[]` pattern: read only by its
   same-bank reader at startup).
3. **data-bank an array into Bank 5** — only if `bankmap.py` shows a gap that
   fits. SDCC rejects pointer-to-array casts, so use a flat `#define` and index
   `pool[slot*STRIDE + i]`.
4. Shrinking `FOV_SLOTS` works but costs remembered levels **and bumps
   SAVE_VER** (the pool is in the save). Last resort — the format is frozen.

## When you change a Bank-5 tenant's size

**Re-audit every Bank-5 `#define` against the new extents — run
`bankmap.py`.** The tenants are hard-coded addresses in five different files
and none of them move when a neighbour grows.

This is not hypothetical: `FOV_SLOTS` 4→12 (0.10.0) grew `fov_pool` over
`PREV_VIS` at `0x6C00`, and for two releases the renderer's repaint copy
silently corrupted two parked levels' fog of war. Also standing: `udg_bitmap`
ends **exactly** at `inv[]` — a 49th tile corrupts the inventory (the mirrored
UDG annex dodges this by living at ids 193-195, past the blocked range).

## Overflow guards already in the build

`build.ps1` refuses an oversized `PAGE_*.bin`; `tools/mktap128.py` refuses an
oversized bank binary. **They are load-bearing** — a silent BANK_3 overflow
shipped a v1.5.0 `.tap` that crashed to BASIC on every template level. If a
guard fires, relocate; never bypass it.

## After the change

Rebuild, run `python tools/bankmap.py` again, and quote the real numbers
(`__BSS_END`, the bank tails) when reporting. Then verify behaviour with
[zrcp-verify] — an overflow that the guards miss shows up as a crash to BASIC
(`PC=0038 SP=ff4c IY=5c3a`) on the affected level type.
