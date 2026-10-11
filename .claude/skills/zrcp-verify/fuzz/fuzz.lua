-- SPDX-License-Identifier: GPL-3.0-or-later
-- Copyright (C) 2026 Leonardo Roman da Rosa
--
-- fuzz.lua - a memory-guard fuzzer for NextHack under MAME (headless, -debug
-- -debugger none); fuzz.ps1 runs it as the -autoboot_script, with the config
-- fuzzcfg.py wrote (FUZZ_CFG). Plays random keys for FUZZ_FRAMES frames on a
-- world pinned by FUZZ_SEED, while
--   * write watchpoints (MAME's debugger) report any game-code write into the
--     memory the game must never write: resident code, the code banks, Bank 5's
--     gaps between tenants, the free space under the stack's reserve -- and, on
--     the 128K, a 0x7FFD value with the shadow-screen or paging-lock bit;
--   * at every wait in the main loop's getkey_rpt (the state is whole there),
--     invariant checks run in Lua -- hero, monsters, pack, pet -- and a renderer
--     ORACLE recomputes every viewport cell from the game state the way a forced
--     full redraw would, and compares it with VIEW_SHADOW and the screen (128K)
--     or with the tilemap (Next);
--   * a 1 kHz sampler on emulated time tracks the lowest SP and the banking
--     trampoline's private stack (tempsp), and a stall/crash watch notes a turn
--     that never returns to the keyboard or a PC stuck in ROM.
-- FUZZ_SCAN=1 visits random levels of random worlds instead (arm_trip below).
-- Everything is deterministic for a seed: rerun it to reproduce a report.
--
-- MAME's debugger reads every number as HEX, and a bare one that starts with a
-- letter (C, D, AF25) as a register name: every number this script hands it
-- goes through hx(), "0x"-prefixed.

local cfg    = dofile(os.getenv("FUZZ_CFG"))
local logf   = io.open(os.getenv("MAME_LOG"), "w")
local function L(s) logf:write(s, "\n") logf:flush() end
local m      = manager.machine
local cpu    = m.devices[":maincpu"]
local mem    = cpu.spaces["program"]
local dbg    = m.debugger
local kb     = m.natkeyboard
local scr
for _, s in pairs(m.screens) do scr = s break end
local T      = cfg.target
local S      = cfg.sym
local TL     = cfg.tile
local SEED   = tonumber(os.getenv("FUZZ_SEED") or "1")
local FRAMES = tonumber(os.getenv("FUZZ_FRAMES") or "60000")
local CLASS  = os.getenv("FUZZ_CLASS") or "a"
local STAIRS_EVERY = tonumber(os.getenv("FUZZ_STAIRS") or "70")
local SCAN = os.getenv("FUZZ_SCAN") == "1"
local grow = nil          -- the altar/fountain grow test in progress (reach_check sets it)
local scan_trips, stale_trips, trip_hits = 0, 0, 0
math.randomseed(SEED)

local function r8(a)  return mem:read_u8(a) end
local function r16(a) return mem:read_u8(a) | (mem:read_u8(a + 1) << 8) end
local function s8(a)  local v = r8(a); if v >= 128 then v = v - 256 end; return v end
local function s16(a) local v = r16(a); if v >= 0x8000 then v = v - 0x10000 end; return v end
local function w8(a, v) mem:write_u8(a, v & 0xFF) end
local function w16(a, v) w8(a, v); w8(a + 1, v >> 8) end
local function hx(v) return string.format("0x%X", v) end   -- "C" alone would be register C
local BANKM  = 0x5B5C     -- 128K: the paged bank, kept by banked_call.asm (and the ROM)
local MAXMON = cfg.maxmon
local DOG    = string.byte("d")

-- ---------------------------------------------------------------- keyboard
local SYM = { [","] = "n", ["."] = "m", [";"] = "o", ['"'] = "p", ["<"] = "r", [">"] = "t",
  [":"] = "z", ["?"] = "c", ["/"] = "v", ["*"] = "b", ["-"] = "j", ["+"] = "k", ["="] = "l" }
local keyfield = {}
for _, port in pairs(m.ioport.ports) do
  for name, f in pairs(port.fields) do
    local first = name:match("^(%S+)")
    if name == "SYMBOL SHIFT" then keyfield.sym = f
    elseif first and #first == 1 then keyfield[first] = f end
  end
end
local chord = nil
local trace, trace_n = {}, 0
local function trace_key(k)
  trace_n = trace_n + 1
  trace[(trace_n - 1) % 40 + 1] = string.format("%s@%d d%d t%d (%d,%d)", k == string.char(13) and "CR" or k,
    m.time:as_double() * 50 // 1, r16(S.dlvl), r16(S.turns), s16(S.hero_x), s16(S.hero_y))
end
local function dump_state()
  local t = {}
  for i = math.max(1, trace_n - 39), trace_n do t[#t + 1] = trace[(i - 1) % 40 + 1] end
  L("  keys: " .. table.concat(t, " "))
  for y = 0, 20 do
    local row = {}
    for x = 0, 79 do row[#row + 1] = string.char(r8(S.lvl + y * 80 + x)) end
    L(string.format("  %02d %s", y, table.concat(row)))
  end
end
local last_key_t = 0
local function send(k)
  trace_key(k)
  last_key_t = m.time:as_double()
  local base = SYM[k] and keyfield[SYM[k]]
  if base and keyfield.sym then
    keyfield.sym:set_value(1); base:set_value(1)
    chord = { keyfield.sym, base, left = 5 }
  else
    kb:post(k)
  end
end

-- weighted random keys: walking most of the time, every command sometimes
local KEYS = {
  { 44, "hjklyubn" }, { 3, "." }, { 2, "s" }, { 4, "," }, { 2, "i" }, { 2, "d" },
  { 2, "q" }, { 2, "e" }, { 2, "r" }, { 2, "z" }, { 2, "t" }, { 2, "a" }, { 1, "Z" },
  { 2, "w" }, { 2, "W" }, { 1, "P" }, { 1, "p" }, { 1, "E" }, { 1, "K" }, { 1, "R" },
  { 1, ";" }, { 1, "D" }, { 1, "?" }, { 2, ">" }, { 1, "<" }, { 3, "\r" }, { 1, " " },
  { 6, "abcdefghijklmnopqrstuvwxyz" }, { 1, "S" }, { 1, "y" }, { 1, "n" },
}
local KW = 0
for _, e in ipairs(KEYS) do KW = KW + e[1] end
local function pick_key()
  local r = math.random() * KW
  for _, e in ipairs(KEYS) do
    r = r - e[1]
    if r <= 0 then local i = math.random(#e[2]); return e[2]:sub(i, i) end
  end
  return "."
end

-- ---------------------------------------------------------------- gifts
-- things worth exercising that a random walk would rarely find: appended to
-- the pack now and then, so the random commands have something to use
local GIFTS = { "pick-axe", "wand of digging", "wand of teleportation", "wand of opening",
  "wand of fire", "wand of sleep", "wand of striking", "scroll of magic mapping",
  "scroll of teleportation", "scroll of genocide", "scroll of charging", "scroll of amnesia",
  "scroll of gold detection", "scroll of scare monster", "scroll of destroy armor",
  "scroll of enchant weapon", "scroll of remove curse", "scroll of identify",
  "potion of blindness", "potion of full healing", "potion of gain level",
  "potion of confusion", "potion of sleeping", "ring of teleport control",
  "ring of teleportitis", "ring of aggravate monster", "ring of stealth", "amulet of ESP",
  "amulet of life", "blindfold", "magic whistle", "unicorn horn", "expensive camera",
  "skeleton key", "spellbook of teleportation", "spellbook of force bolt",
  "spellbook of sleep", "battle-axe", "large shield", "Stormbringer", "Sting", "corpse",
  "carrot", "silver saber", "helmet", "cloak" }
local CORPSES = "ekSaDiWlnZT"
local gifts = 0
local function gift()
  local n = r8(S.inv_count)
  if n >= cfg.maxinv - 4 then return end          -- leave the pickups some room
  local name = GIFTS[math.random(#GIFTS)]
  local ot = cfg.objs[name]
  if not ot then return end
  local ench = 0
  if name:match("^wand") then ench = math.random(2, 7)
  elseif name == "expensive camera" then ench = math.random(1, 20)
  elseif name == "corpse" then local k = math.random(#CORPSES); ench = CORPSES:byte(k) end
  local r = math.random(10)
  local buc = (r == 1) and 2 or (r == 2) and 1 or 0
  local a = cfg.inv + n * 5                       -- obj_t: otyp, ench, ero, worn, buc
  w8(a, ot); w8(a + 1, ench); w8(a + 2, 0); w8(a + 3, 0); w8(a + 4, buc)
  w8(S.inv_count, n + 1)
  gifts = gifts + 1
end

-- ---------------------------------------------------------------- reports
local seen_kind, count_kind = {}, {}
local snaps = 0
local function report(kind, detail)
  count_kind[kind] = (count_kind[kind] or 0) + 1
  if (seen_kind[kind] or 0) < 4 then
    seen_kind[kind] = (seen_kind[kind] or 0) + 1
    L(string.format("VIOLATION %s @frame %d dlvl %d turns %d: %s", kind, m.time:as_double() * 50 // 1,
      r16(S.dlvl), r16(S.turns), detail))
    if seen_kind[kind] == 1 and os.getenv("FUZZ_DUMP") == "1" then dump_state() end
    if seen_kind[kind] == 1 and snaps < 40 then
      snaps = snaps + 1
      scr:snapshot(string.format("v%02d_%s.png", snaps, kind:gsub("%W", "_")))
    end
  end
end

-- ---------------------------------------------------------------- the oracle
-- tile_for (level.c), as draw_map would ask it
local function tile_for(c, mines)
  if c == 46 then return TL.T_FLOOR end            -- .
  if c == 94 then return TL.T_TRAP end             -- ^
  if c == 95 then return TL.T_ALTAR end            -- _
  if c == 35 then return TL.T_CORR end             -- #
  if c == 45 or c == 124 then return mines and TL.T_MINEWALL or TL.T_WALL end
  if c == 43 then return TL.T_DOOR end             -- +
  if c == 60 then return TL.T_SUP end              -- <
  if c == 62 then return TL.T_SDOWN end            -- >
  if c == 36 then return TL.T_GOLD end             -- $
  if c == 37 then return TL.T_FOOD end             -- %
  if c == 41 then return TL.T_WEAPON end           -- )
  if c == 91 then return TL.T_ARMOR end            -- [
  if c == 33 then return TL.T_POTION end           -- !
  if c == 63 then return TL.T_SCROLL end           -- ?
  if c == 61 then return TL.T_RING end             -- =
  if c == 47 then return TL.T_WAND end             -- /
  if c == 38 then return TL.T_BOOK end             -- &
  if c == 123 then return TL.T_FOUNTAIN end        -- {
  if c == 34 then return TL.T_AMULET end           -- "
  if c == 118 then return TL.T_MINEHOLE end        -- v
  if c == 42 then return TL.T_LUCKSTONE end        -- *
  if c == 40 then return TL.T_TOOL end             -- (
  return TL.T_ROCK
end

local function gamestate()
  local g = {}
  g.dlvl = r16(S.dlvl); g.hx = s16(S.hero_x); g.hy = s16(S.hero_y)
  g.mines = g.dlvl >= 51
  g.mcount = r8(S.mcount)
  g.mons = {}
  for i = 0, math.min(g.mcount, MAXMON) - 1 do
    if r8(S.m_alive + i) ~= 0 then
      g.mons[#g.mons + 1] = { i = i, x = r8(S.m_x + i), y = r8(S.m_y + i), t = r8(S.m_type + i),
                              face = r8(S.m_face + i) }
    end
  end
  local slot = r8(S.cur_slot)
  g.seen = cfg.fov_pool + slot * cfg.fov_bytes
  g.vis  = S.vis_now
  g.sensed = r8(S.st_blind) ~= 0 and ((r8(S.intrinsics) & 4) ~= 0 or r8(S.amu_esp) ~= 0)
  local sr = s8(S.shop_room)
  if sr >= 0 then
    g.sx, g.sy = r8(S.r_x + sr), r8(S.r_y + sr)
    g.sx1, g.sy1 = g.sx + r8(S.r_w + sr) - 1, g.sy + r8(S.r_h + sr) - 1
  end
  g.hface = r8(S.hero_face)
  return g
end
local function bitat(base, idx) return (r8(base + (idx >> 3)) >> (idx & 7)) & 1 end
local function shopwall(g, t, x, y)
  if g.sx and (t == TL.T_WALL or t == TL.T_MINEWALL) and x >= g.sx and x <= g.sx1 and y >= g.sy and y <= g.sy1 then
    return TL.T_SHOPWALL
  end
  return t
end
local function montile(tc) return cfg.mon[string.char(tc)] or 0 end

local last_oracle = ""
local function oracle128(g)
  local vx = r8(S.vx_origin)
  local ink = function(t) return r8(S.udg_ink + t - 128) end
  local bad, badscr = {}, {}
  for y = 0, 20 do
    for sc = 0, 31 do
      local x = vx + sc
      local idx = y * 80 + x
      local et, ea
      local mon = nil
      for _, mo in ipairs(g.mons) do
        if mo.x == x and mo.y == y and not (x == g.hx and y == g.hy) and
           (bitat(g.vis, idx) == 1 or g.sensed) then mon = mo end      -- the last drawn wins
      end
      if x == g.hx and y == g.hy then
        et = (g.hface == 0) and TL.T_HERO_R or TL.T_HERO
        ea = ink(TL.T_HERO) | 0x40
      elseif mon then
        local mt = montile(mon.t)
        et = mt
        if mon.face ~= 0 then
          if mt == TL.T_DOG then et = TL.T_DOG_R elseif mt == TL.T_RAT then et = TL.T_RAT_R end
        end
        ea = ink(mt) | 0x40
      elseif bitat(g.seen, idx) == 0 then
        et, ea = TL.T_ROCK, 0
      else
        local c = r8(S.lvl + idx)
        et = shopwall(g, tile_for(c, g.mines), x, y)
        ea = ink(et) | ((bitat(g.vis, idx) == 1) and 0x40 or 0)
      end
      local sh = cfg.view_shadow + (y * 32 + sc) * 2
      local st, sa = r8(sh), r8(sh + 1)
      if st ~= et or sa ~= ea then
        bad[#bad + 1] = string.format("(%d,%d)c=%q exp %d/%02X shad %d/%02X", x, y,
          string.char(r8(S.lvl + idx)), et, ea, st, sa)
      end
      -- the screen must show what the shadow says
      local row = y + 1
      local lo = ((row & 7) << 5) + sc
      local ab = 0x5800 + ((row >> 3) << 8) + lo
      local bb = 0x4000 + ((row & 0x18) << 8) + lo
      local ok = r8(ab) == sa
      if ok then
        local gl = cfg.udg + (st - 128) * 8
        for r = 0, 7 do if r8(bb + r * 256) ~= r8(gl + r) then ok = false break end end
      end
      if not ok then
        -- the farlook / teleport-control cursor: a ROM-font X over the cell
        local isx = true
        for r = 0, 7 do if r8(bb + r * 256) ~= r8(0x3C00 + 88 * 8 + r) then isx = false break end end
        if not isx then badscr[#badscr + 1] = string.format("(%d,%d)", x, y) end
      end
    end
  end
  local key = table.concat(bad, ";") .. "|" .. table.concat(badscr, ";")
  if key ~= last_oracle then
    last_oracle = key
    if #bad > 0 then
      report("oracle-shadow", string.format("%d cells, vx %d hero (%d,%d): %s", #bad, vx, g.hx, g.hy,
        table.concat(bad, " ", 1, math.min(#bad, 6))))
    end
    if #badscr > 0 then
      report("oracle-screen", string.format("%d cells differ from the shadow: %s", #badscr,
        table.concat(badscr, " ", 1, math.min(#badscr, 10))))
    end
  end
end

local function oracle_next(g)
  local bad = {}
  for y = 0, 20 do
    for x = 0, 79 do
      local idx = y * 80 + x
      local et, ea = nil, 0
      local first = nil
      for _, mo in ipairs(g.mons) do
        if mo.x == x and mo.y == y then first = mo break end
      end
      if x == g.hx and y == g.hy then
        et = TL.T_HERO
        if g.hface == 0 then ea = 0x08 end
      elseif g.sensed and first then
        et = montile(first.t)
      elseif bitat(g.seen, idx) == 0 then
        et = TL.T_ROCK
      elseif bitat(g.vis, idx) == 1 then
        et = first and montile(first.t) or tile_for(r8(S.lvl + idx), g.mines)
      else
        et = tile_for(r8(S.lvl + idx), g.mines); ea = 0x10
      end
      if et ~= TL.T_HERO and (et == TL.T_DOG or et == TL.T_RAT) and first and first.face ~= 0 then
        ea = ea | 0x08
      end
      et = shopwall(g, et, x, y)
      local a = 0x6000 + ((y + 1) * 80 + x) * 2
      local cursor = r8(a) == 88 and r8(a + 1) == 0xE0      -- the farlook X
      if not cursor and (r8(a) ~= et or r8(a + 1) ~= ea) then
        bad[#bad + 1] = string.format("(%d,%d)c=%q exp %d/%02X tm %d/%02X", x, y,
          string.char(r8(S.lvl + idx)), et, ea, r8(a), r8(a + 1))
      end
    end
  end
  local key = table.concat(bad, ";")
  if key ~= last_oracle then
    last_oracle = key
    if #bad > 0 then
      report("oracle-tilemap", string.format("%d cells, hero (%d,%d): %s", #bad, g.hx, g.hy,
        table.concat(bad, " ", 1, math.min(#bad, 6))))
    end
  end
end

-- ---------------------------------------------------------------- invariants
local function walkable(c) return not (c == 124 or c == 45 or c == 32) end

-- On a level's first check: can the hero WALK to the way on? A flood over every
-- walkable cell (8 ways, as try_move moves; a locked door counts, the boot
-- opens it) from the hero must reach the down stairs, the mine entrance on
-- Dlvl 2 and the Amulet's cell on Dlvl 50. A level that fails is a soft lock.
local reach_seen = {}
local reach_levels = 0
local function reach_check(g)
  local key = g.dlvl .. ":" .. r16(S.world_seed)
  if reach_seen[key] then return end
  reach_seen[key] = true
  reach_levels = reach_levels + 1
  local seen, q, h = {}, {}, 1
  local start = g.hy * 80 + g.hx
  seen[start] = true; q[1] = start
  while h <= #q do
    local k = q[h]; h = h + 1
    local x, y = k % 80, k // 80
    for dy = -1, 1 do for dx = -1, 1 do
      local nx, ny = x + dx, y + dy
      if nx >= 0 and nx < 80 and ny >= 0 and ny < 21 then
        local nk = ny * 80 + nx
        if not seen[nk] and walkable(r8(S.lvl + nk)) then seen[nk] = true; q[#q + 1] = nk end
      end
    end end
  end
  local function need(x, y, what)
    if not seen[y * 80 + x] then
      report("unreachable-" .. what, string.format("hero (%d,%d) cannot walk to %s at (%d,%d) c=%q; %d cells reachable",
        g.hx, g.hy, what, x, y, string.char(r8(S.lvl + y * 80 + x)), #q))
    end
  end
  local dx, dy = r8(S.dn_x), r8(S.dn_y)
  local c = r8(S.lvl + dy * 80 + dx)
  if g.dlvl ~= 54 and (c == 62 or c == 34) then need(dx, dy, "downstairs") end
  local ux, uy = r8(S.up_x), r8(S.up_y)
  if r8(S.lvl + uy * 80 + ux) == 60 then need(ux, uy, "upstairs") end
  if g.dlvl == 2 then need(r8(S.mn_x), r8(S.mn_y), "mine-entrance") end
  -- the ways on and off: one '<' everywhere, one '>' except at the two
  -- bottoms, the mine hole only on Dlvl 2, the Amulet only on Dlvl 50
  local n = { [60] = 0, [62] = 0, [118] = 0 }
  for k = 0, 80 * 21 - 1 do
    local ch = r8(S.lvl + k)
    if n[ch] then n[ch] = n[ch] + 1 end
  end
  local bottom = (g.dlvl == 50 or g.dlvl == 54)
  local want = { [60] = 1, [62] = bottom and 0 or 1, [118] = (g.dlvl == 2) and 1 or 0 }
  if g.dlvl == 50 and r8(S.has_amulet) == 0 and c ~= 34 then
    report("amulet-cell", string.format("Dlvl 50 dn (%d,%d) holds %q", dx, dy, string.char(c)))
  end
  for ch, w in pairs(want) do
    if n[ch] ~= w then
      report("count-" .. string.char(ch), string.format("%d of %q on dlvl %d (want %d) seed %04X",
        n[ch], string.char(ch), g.dlvl, w, r16(S.world_seed)))
    end
  end
  if not bottom and c ~= 62 then
    report("dn-cell", string.format("dn (%d,%d) holds %q", dx, dy, string.char(c)))
  end
  -- an altar or fountain whose spot holds a generated pile: take the pile,
  -- come back, and the spot must still hold no altar/fountain. The spots are
  -- place_altar's and place_fountain's hashes (nexthack_lvl.c) and eff_depth's
  -- mines rule, mirrored: change those and this goes quietly blind.
  if SCAN and not grow then
    local ws, dl, rc = r16(S.world_seed), g.dlvl, r8(S.rcount)
    local function centre(room)
      return r8(S.r_x + room) + r8(S.r_w + room) // 2, r8(S.r_y + room) + r8(S.r_h + room) // 2
    end
    local cand = {}
    if rc > 0 then
      local h = (ws + dl * 0x9E37) & 0xFFFF
      if h % 5 == 0 then local x, y = centre((h >> 3) % rc); cand[#cand + 1] = { x, y, "altar", 95 } end
      local ed = (dl >= 51) and (dl - 51 + 3) or dl
      h = (ws * 3 + dl * 0x2C9F) & 0xFFFF
      if ed >= 2 and h % 4 == 0 then local x, y = centre((h >> 4) % rc); cand[#cand + 1] = { x, y, "fountain", 123 } end
    end
    for _, cd in ipairs(cand) do
      local ch = string.char(r8(S.lvl + cd[2] * 80 + cd[1]))
      local sr = s8(S.shop_room)
      local inshop = sr >= 0 and cd[1] >= r8(S.r_x + sr) and cd[1] < r8(S.r_x + sr) + r8(S.r_w + sr)
                     and cd[2] >= r8(S.r_y + sr) and cd[2] < r8(S.r_y + sr) + r8(S.r_h + sr)
      if not inshop and ("$)[!?=/&(%"):find(ch, 1, true) then
        grow = { x = cd[1], y = cd[2], kind = cd[3], code = cd[4], d = dl, seed = ws, phase = "pick", ch = ch }
        L(string.format("grow test: %s spot (%d,%d) on dlvl %d seed %04X holds %q", cd[3], cd[1], cd[2], dl, ws, ch))
        break
      end
    end
  end
  if r8(S.lvl + uy * 80 + ux) ~= 60 then
    report("up-cell", string.format("up (%d,%d) holds %q", ux, uy, string.char(r8(S.lvl + uy * 80 + ux))))
  end
end
-- A scan trip never pokes the game before its key is taken: a misread key
-- would then run turns with a new depth on the old floor. Instead a one-shot
-- write watchpoint on el_life fires at build_level's "el_life = 0", which
-- comes after go_down/go_up changed dlvl and before gen_level reads it, and
-- its action sets the depth and the world and forgets the last world's
-- stashes, tunnels, masks and open doors (they would land on this one's
-- walls). A key that never reaches the stairs code arms nothing that fires.
local trip = nil
local trip_serial, last_trip_fired = 0, false
local grow_tests, grow_passed = 0, 0
local missed_logged = 0
local function last_keys(n)
  local t = {}
  for i = math.max(1, trace_n - n + 1), trace_n do t[#t + 1] = trace[(i - 1) % 40 + 1] end
  return table.concat(t, " ")
end
local function arm_trip(odl, D, keep_seed)
  local seed = keep_seed or math.random(1, 0xFFFF)
  dbg:command("do temp0 = 0")
  local cmds = {
    "do temp0 = 1",
    "do b@" .. hx(S.dlvl) .. " = " .. hx(D), "do b@" .. hx(S.dlvl + 1) .. " = 0",
    "do w@" .. hx(S.world_seed) .. " = " .. hx(seed) }
  if not keep_seed then             -- a new world forgets the last one
    for _, c in ipairs({ "stash", "gold_taken", "item_taken", "mon_dead", "door_open" }) do
      cmds[#cmds + 1] = "fill " .. hx(S[c]) .. "," .. hx(cfg.size[c]) .. ",0"
    end
    cmds[#cmds + 1] = "do b@" .. hx(S.floor_n) .. " = 0"
    cmds[#cmds + 1] = "do b@" .. hx(cfg.dug_pool) .. " = 0"     -- the tunnels' count
  end
  trip_serial = trip_serial + 1
  last_trip_fired = false
  cmds[#cmds + 1] = 'printf "TRIP n=' .. trip_serial .. '"'
  cmds[#cmds + 1] = "g"
  local act = table.concat(cmds, "; ")
  local cond = "temp0 == 0 && wpdata == 0 && b@" .. hx(S.dlvl) .. " != " .. hx(odl)
  trip = { D = D, seed = seed, n = trip_serial, id = cpu.debug:wpset(mem, "w", S.el_life, 1, cond, act) }
  scan_trips = scan_trips + 1
end
local function end_trip()            -- last_trip_fired stays until the next arm_trip
  if not trip then return end
  cpu.debug:wpclear(trip.id)
  -- did the action run? its serial is in the debugger's console log
  local cl, tag = dbg.consolelog, "TRIP n=" .. trip.n
  for i = #cl, math.max(1, #cl - 40), -1 do
    if cl[i] == tag then last_trip_fired = true; break end
  end
  if last_trip_fired and r16(S.dlvl) == trip.D and r16(S.world_seed) == trip.seed then trip_hits = trip_hits + 1
  elseif missed_logged < 12 then
    missed_logged = missed_logged + 1
    local t = {}
    for i = math.max(1, trace_n - 5), trace_n do t[#t + 1] = trace[(i - 1) % 40 + 1] end
    L("trip not taken: " .. table.concat(t, " "))
  end
  trip = nil
end
local checks = 0
local cursor_skips = 0
local poked, lost_keys = nil, 0
local function check()
  checks = checks + 1
  end_trip()
  local g = gamestate()
  if g.dlvl < 1 or g.dlvl > 54 then report("dlvl-range", tostring(g.dlvl)) return end
  if g.hx < 1 or g.hx > 78 or g.hy < 1 or g.hy > 19 then
    report("hero-bounds", string.format("(%d,%d)", g.hx, g.hy)) return
  end
  local hc = r8(S.lvl + g.hy * 80 + g.hx)
  if not walkable(hc) then report("hero-in-rock", string.format("(%d,%d) c=%q", g.hx, g.hy, string.char(hc))) end
  if g.mcount > MAXMON then report("mcount", tostring(g.mcount)) end
  local occ = {}
  for _, mo in ipairs(g.mons) do
    if mo.x > 79 or mo.y > 20 then
      report("mon-bounds", string.format("slot %d %q at (%d,%d)", mo.i, string.char(mo.t), mo.x, mo.y))
    else
      local c = r8(S.lvl + mo.y * 80 + mo.x)
      if not walkable(c) then
        report("mon-in-rock", string.format("slot %d %q at (%d,%d) c=%q", mo.i, string.char(mo.t), mo.x, mo.y, string.char(c)))
      end
      local k = mo.y * 80 + mo.x
      if occ[k] then
        report("mon-stacked", string.format("slots %d and %d at (%d,%d)", occ[k], mo.i, mo.x, mo.y))
      end
      occ[k] = mo.i
      if mo.x == g.hx and mo.y == g.hy then
        report("mon-on-hero", string.format("slot %d %q at (%d,%d)", mo.i, string.char(mo.t), mo.x, mo.y))
      end
    end
  end
  local ic = r8(S.inv_count)
  if ic > cfg.maxinv then report("inv-count", tostring(ic))
  else
    for k = 0, ic - 1 do
      local ot = r8(cfg.inv + k * 5)
      if ot >= cfg.numobj then report("inv-otyp", string.format("slot %d otyp %d", k, ot)) end
    end
  end
  local php, pmax = r8(S.php), r8(S.pmaxhp)
  if php > pmax then report("hp-over-max", string.format("%d/%d", php, pmax)) end
  local pet = s8(S.pet_idx)
  if pet >= g.mcount and pet >= 0 then report("pet-idx", string.format("%d mcount %d", pet, g.mcount)) end
  -- pet_idx -1 with have_pet set is legal: place_pet lets the dog sit a level
  -- out when the arrival neighbourhood is packed (or MAXMON is reached)
  if r8(S.have_pet) ~= 0 and pet >= 0 then
    if r8(S.m_alive + pet) == 0 or r8(S.m_type + pet) ~= DOG then
      report("pet-lost", string.format("have_pet but slot %d alive %d type %d", pet,
        pet >= 0 and r8(S.m_alive + pet) or -1, pet >= 0 and r8(S.m_type + pet) or -1))
    end
  end
  if r8(cfg.dug_pool) > cfg.dug_max then report("dug-count", tostring(r8(cfg.dug_pool))) end
  if poked and poked.d == g.dlvl and poked.x == g.hx and poked.y == g.hy and
     poked.t == r16(S.turns) then
    lost_keys = lost_keys + 1          -- our own teleport, not yet drawn: no oracle
    poked = nil
    return
  end
  poked = nil
  reach_check(g)
  -- getkey_rpt also waits under the farlook / teleport-control cursor
  -- (cursor_pick), mid-turn and before the turn's redraw: no oracle there.
  -- The PC cannot tell the two waits apart; a return address into
  -- cursor_pick among the top stack words can.
  local sp = cpu.state["SP"].value
  for k = 0, 30, 2 do
    local w = r16(sp + k)
    if w >= cfg.cursor[1] and w < cfg.cursor[2] then cursor_skips = cursor_skips + 1; return end
  end
  if T == "zx128" then oracle128(g) else oracle_next(g) end
end

-- ---------------------------------------------------------------- watchpoints
local function arm()
  dbg.execution_state = "run"
  local bank = (T == "zx128") and (",b@" .. hx(BANKM)) or ",0"
  for _, gd in ipairs(cfg.guard) do
    local lo, hi, label = gd[1], gd[2], gd[3]
    local cond = "pc >= 0x4000"                  -- the ROM's own writes (IM1, sysvars) are fine
    if T == "zx128" and lo <= BANKM and BANKM < hi then
      cond = cond .. " && wpaddr != " .. hx(BANKM)  -- the trampoline records the bank there
    end
    local act = string.format('printf "WP %s a=%%04X d=%%02X pc=%%04X sp=%%04X bk=%%02X t=%%X",wpaddr,wpdata,pc,sp%s,totalcycles; g', label, bank)
    cpu.debug:wpset(mem, "w", lo, hi - lo, cond, act)
  end
  -- the positive control: a guard that MUST fire, or the run proves nothing.
  -- On the 128K the ROM's IM1 handler bumps FRAMES every frame; the bare .nex
  -- runs with interrupts off, so there the game's own message line (row 0 of
  -- the tilemap, written on every msg) stands in.
  local ca = (T == "zx128") and 0x5C78 or 0x6000
  local cc = (T == "zx128") and "wpdata == 0x0" or ("wpdata == " .. hx(string.byte("Y")))  -- every 256 frames / a 'Y' message
  cpu.debug:wpset(mem, "w", ca, 1, cc, 'printf "WP control pc=%04X t=%X",pc,totalcycles; g')
  if T == "zx128" then
    -- bit 3 is the shadow screen and bit 5 the paging lock: neither may ever
    -- be set (CLAUDE.md, RAM-expansion interfaces). Bit 4, the ROM, always is.
    cpu.debug:wpset(cpu.spaces["io"], "w", 0x7FFD, 1, "(wpdata & 0x28) != 0x0",
      'printf "WP port7ffd d=%02X pc=%04X t=%X",wpdata,pc,totalcycles; g')
  end
  L("armed " .. #cfg.guard .. " guard ranges")
end

-- ---------------------------------------------------------------- the sampler
local minsp, mintemp, maxnest = 0xFFFF, 0xFFFF, 0
local cover = {}
local sampling = false
local function sampler()
  while sampling do
    emu.wait(1.0 / 1000)
    local sp = cpu.state["SP"].value
    if sp >= 0x8000 and sp < minsp then minsp = sp end
    local ts = r16(S.tempsp)
    if ts >= 0x8000 and ts < mintemp then mintemp = ts end
    local pc = cpu.state["PC"].value
    local key = pc
    if pc >= 0xC000 and T == "zx128" then key = ((r8(BANKM) & 7) << 16) | pc end
    cover[key] = (cover[key] or 0) + 1
  end
end

-- ---------------------------------------------------------------- the run
local frame, phase, at, play_start = 0, "boot", 0, 0
local idle_frames, busy_frames, rom_frames = 0, 0, 0
local actions, next_act = 0, 0
local levels, maxd, deaths, keepalive = {}, 0, 0, 0
local last_turns = 0
local last_stairs = 0
local last_gift = 0
local stalled = false
local idle = cfg.idle
local function in_r(pc, r) return pc >= r[1] and pc < r[2] end
local function is_idle(pc) for _, r in ipairs(idle) do if in_r(pc, r) then return true end end return false end

local wp, wp_seen = {}, {}
local function drain()
  local cl = dbg.consolelog
  for i = 1, #cl do
    local s = cl[i]
    if s:match("^WP ") and not wp_seen[s] then
      wp_seen[s] = true
      local key = s:gsub(" a=%x+", ""):gsub(" d=%x+", ""):gsub(" sp=%x+", ""):gsub(" t=%x+", "")
      if not wp[key] then
        wp[key] = { n = 0, ex = s }
        if not key:match("^WP control") then
          L(string.format("HIT @frame %d dlvl %d: %s", frame, r16(S.dlvl), s))
        end
      end
      wp[key].n = wp[key].n + 1
    end
  end
end

local function summary()
  L(string.format("SUMMARY seed %d frames %d actions %d checks %d deaths %d keepalive %d gifts %d", SEED, frame, actions, checks, deaths, keepalive, gifts))
  L("stairs keys lost " .. lost_keys .. "  levels walk-checked " .. reach_levels .. "  scan trips " .. scan_trips .. " taken " .. trip_hits .. "  grow tests " .. grow_tests .. " passed " .. grow_passed .. "  cursor skips " .. cursor_skips)
  local lv = {}
  for d in pairs(levels) do lv[#lv + 1] = d end
  table.sort(lv)
  L("levels " .. table.concat(lv, ","))
  L(string.format("min SP %04X (reserve floor %04X, BSS end %04X)  min tempsp %04X (main stack top %04X)",
    minsp, cfg.stack_floor, cfg.bss_end, mintemp, cfg.sp_init - cfg.banking_stack))
  for k, v in pairs(count_kind) do L(string.format("count %s %d", k, v)) end
  drain()
  local cf = io.open(os.getenv("MAME_LOG") .. ".cover", "w")
  for k, v in pairs(cover) do cf:write(string.format("%X %d\n", k, v)) end
  cf:close()
  for k, v in pairs(wp) do L(string.format("WATCH %dx %s   e.g. %s", v.n, k, v.ex)) end
end

frame_sub = emu.add_machine_frame_notifier(function()
  frame = frame + 1
  if chord then
    chord.left = chord.left - 1
    if chord.left <= 0 then chord[1]:clear_value(); chord[2]:clear_value(); chord = nil end
  end
  local pc = cpu.state["PC"].value
  if phase == "boot" then
    if frame == 2 then dbg.execution_state = "run" end
    if T == "zx128" then
      if frame == 150 then kb:post("\r") end
      if frame == 200 then m.cassettes[":cassette"]:play() end
      if frame > 200 and pc >= 0x8000 and at == 0 then at = frame + 100 end
    else
      if pc >= 0x8000 and at == 0 then at = frame + 150 end
    end
    if at > 0 and frame >= at then phase = "title"; kb:post(" "); at = frame + 100 end
    return
  end
  if phase == "title" then
    if frame >= at then
      local s16v = (SEED * 40503) & 0xFFFF
      if s16v == 0 then s16v = 0xACE1 end
      w16(S.world_seed, s16v); w16(S.rng, s16v)
      kb:post(CLASS)
      L(string.format("world seed %04X class %s", s16v, CLASS))
      phase = "pick"; at = frame + 150
    end
    return
  end
  if phase == "pick" then
    if frame >= at then
      arm()
      sampling = true
      local ok, err = coroutine.resume(coroutine.create(sampler))
      if not ok then L("sampler: " .. tostring(err)) end
      phase = "play"; play_start = frame; L("play @frame " .. frame)
    end
    return
  end
  if phase ~= "play" then return end

  if frame >= play_start + FRAMES then
    sampling = false
    summary()
    logf:close(); m:exit()
    phase = "done"
    return
  end

  if frame % 50 == 0 then drain() end
  local idl = is_idle(pc)
  if idl then idle_frames = idle_frames + 1; busy_frames = 0 else idle_frames = 0; busy_frames = busy_frames + 1 end
  if busy_frames == 1500 and not stalled then
    stalled = true
    report("stall", string.format("30 s without a key wait: PC %04X SP %04X resting %d bank %d",
      pc, cpu.state["SP"].value, r8(S.resting), T == "zx128" and (r8(BANKM) & 7) or -1))
  end
  if idl then stalled = false end
  if pc < 0x4000 then rom_frames = rom_frames + 1 else rom_frames = 0 end
  if rom_frames == 100 then report("in-rom", string.format("PC %04X for 100 frames (reset?)", pc)) end

  if idle_frames >= 2 and frame >= next_act and not chord then
    if in_r(pc, cfg.main_idle) then
      local d = r16(S.dlvl)
      levels[d] = true
      if d > maxd and d < 51 then maxd = d end
      local t = r16(S.turns)
      if t < last_turns then deaths = deaths + 1 end
      last_turns = t
      check()
      -- keep the run going: top up HP and food (logged as a count only)
      local php, pmax = r8(S.php), r8(S.pmaxhp)
      if SCAN then w8(S.pmaxhp, 250); w8(S.php, 250)   -- the scan wants levels, not fights
      elseif php < pmax // 2 then w8(S.php, pmax); keepalive = keepalive + 1 end
      if s16(S.nutrition) < 400 then w16(S.nutrition, 900); keepalive = keepalive + 1 end
      if actions - last_gift >= 25 then last_gift = actions; gift() end
      -- the grow test (reach_check found a pile on an altar/fountain spot):
      -- poke the hero onto it, ',', then revisit the same level via the stairs
      if grow and m.time:as_double() - last_key_t >= 0.16 then
        local cell = r8(S.lvl + grow.y * 80 + grow.x)
        if grow.phase == "pick" and (d ~= grow.d or r16(S.world_seed) ~= grow.seed) then
          L("grow test abandoned: the level changed before the pickup"); grow = nil
        elseif grow.phase == "pick" then
          w16(S.hero_x, grow.x); w16(S.hero_y, grow.y)
          poked = { d = d, x = grow.x, y = grow.y, t = r16(S.turns) }
          grow.phase = "picked"; grow.tries = (grow.tries or 0) + 1
          send(","); actions = actions + 1; next_act = frame + 6; idle_frames = 0
          return
        elseif grow.phase == "picked" then
          if d ~= grow.d or r16(S.world_seed) ~= grow.seed then L("grow test abandoned: left the level"); grow = nil
          elseif cell ~= 46 then
            if grow.tries < 3 then grow.phase = "pick" else L("grow test abandoned: pile not taken, cell " .. string.char(cell)); grow = nil end
          else
            local tx, ty, key = r8(S.dn_x), r8(S.dn_y), ">"
            if d == 50 or d == 54 then tx, ty, key = r8(S.up_x), r8(S.up_y), "<" end
            w16(S.hero_x, tx); w16(S.hero_y, ty)
            poked = { d = d, x = tx, y = ty, t = r16(S.turns) }
            arm_trip(d, d, grow.seed)
            L(string.format("grow test: picked up, revisiting via %s from (%d,%d)", key, tx, ty))
            grow.phase = "back"
            send(key); actions = actions + 1; next_act = frame + 6; idle_frames = 0
            return
          end
        elseif grow.phase == "back" then
          if last_trip_fired and d == grow.d and r16(S.world_seed) == grow.seed then
            grow_tests = grow_tests + 1
            if cell == grow.code then
              report("grew-" .. grow.kind, string.format("(%d,%d) on dlvl %d seed %04X: a %s stands where the %q was taken",
                grow.x, grow.y, grow.d, grow.seed, grow.kind, grow.ch))
            else
              grow_passed = grow_passed + 1
              L(string.format("grow test passed: (%d,%d) holds %q after the revisit", grow.x, grow.y, string.char(cell)))
            end
          else
            L(string.format("grow test abandoned: the revisit did not happen (dlvl %d seed %04X turns %d; keys %s)",
              d, r16(S.world_seed), r16(S.turns), last_keys(8)))
          end
          grow = nil
        end
      end
      -- every so often take the stairs from wherever we are
      if actions - last_stairs >= STAIRS_EVERY then
        -- a trip's chord must not overlap the last key, still held by MAME's
        -- natural keyboard: the game would see neither (8 frames of quiet)
        if SCAN and m.time:as_double() - last_key_t < 0.16 then return end
        last_stairs = actions
        local function occupied(x, y)
          for i = 0, math.min(r8(S.mcount), MAXMON) - 1 do
            if r8(S.m_alive + i) ~= 0 and r8(S.m_x + i) == x and r8(S.m_y + i) == y then return true end
          end
          return false
        end
        local tx, ty, key, pdl
        if SCAN then
          -- the stairs at hand; WHERE they lead is decided inside build_level
          if d == 50 or d == 54 then tx, ty, key = r8(S.up_x), r8(S.up_y), "<"
          else tx, ty, key = r8(S.dn_x), r8(S.dn_y), ">" end
          -- a sleeper on the '>' would hold the scan on this level for good
          -- (it once ate 11000 of a run's 12500 frames): climb instead. Not
          -- from Dlvl 1 (nowhere to go, or a win with the Amulet) nor Mine:1
          -- (go_up lands the hero on the mine hole, which only Dlvl 2 has)
          if key == ">" and occupied(tx, ty) and d > 1 and d ~= 51 then
            L(string.format("A %d stairs > blocked at (%d,%d) on dlvl %d: climbing", frame, tx, ty, d))
            tx, ty, key = r8(S.up_x), r8(S.up_y), "<"
          end
          pdl = math.random(1, 54)
          -- nothing climbs INTO Dlvl 50: go_up would land on its down cell,
          -- the Amulet's, where the keeper stands
          if key == "<" and pdl == 50 then pdl = 49 end
        elseif d == 50 or d == 54 then tx, ty, key = r8(S.up_x), r8(S.up_y), "<"
        elseif d >= 51 and math.random() < 0.5 then tx, ty, key = r8(S.up_x), r8(S.up_y), "<"
        elseif d == 2 and math.random() < 0.15 then tx, ty, key = r8(S.mn_x), r8(S.mn_y), ">"
        else tx, ty, key = r8(S.dn_x), r8(S.dn_y), ">" end
        if not occupied(tx, ty) and tx > 0 and ty > 0 then
          w16(S.hero_x, tx); w16(S.hero_y, ty)
          poked = { d = d, x = tx, y = ty, t = r16(S.turns) }
          if pdl then arm_trip(d, pdl) end
          L(string.format("A %d stairs %s from dlvl %d", frame, key, d))
          send(key); actions = actions + 1; next_act = frame + 6; idle_frames = 0
          return
        end
      end
    end
    local k = pick_key()
    send(k)
    actions = actions + 1
    next_act = frame + 3
    idle_frames = 0
  end
end)
L("fuzz start seed " .. SEED .. " target " .. T)
