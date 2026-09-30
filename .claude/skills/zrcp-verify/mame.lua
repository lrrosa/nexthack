-- SPDX-License-Identifier: GPL-3.0-or-later
-- Copyright (C) 2026 Leonardo Roman da Rosa
--
-- mame.lua - the in-emulator half of mame.ps1. MAME runs it as its
-- -autoboot_script; it plays a step file one frame-driven step at a time and
-- logs what it reads. MAME has no debug socket like ZRCP, so a MAME test is a
-- script written up front, not a conversation -- mame.ps1 writes the file.
--
-- Every address arrives here in DECIMAL (mame.ps1 resolves @sym and 0x..).
-- Steps, one per line:
--   wait <sec>            let <sec> seconds of emulated time pass (50 frames/s)
--   frames <n>            the same, in frames
--   key <text>            type through the natural keyboard (\r = Enter, \s = space);
--                         a lone symbol (, < > ; ? ...) is pressed as its chord
--   coded <text>          post_coded, for {UP} {DOWN} {LEFT} {RIGHT}
--   play                  start the cassette
--   waittape              until the cassette has played to its end
--   waitpc <addr>         until PC >= addr (0x8000 = the game is running)
--   load <path>           mount a snapshot now (the Next's .nex, after boot)
--   peek <addr> <n> <lbl> log n bytes, hex, under a label
--   poke <addr> <v>...    write bytes (decimal values)
--   msg                   log row 0 (the message line) decoded as text
--   snap <name>           save the screen as <name>.png in the snapshot dir
--   pc | log <text> | exit
--
-- Reads happen between frames, outside emulated time, so unlike ZRCP they do
-- not stretch what they measure.

local log = io.open(os.getenv("MAME_LOG"), "w")
local function L(s) log:write(s, "\n") log:flush() end

local steps = {}
for line in io.lines(os.getenv("MAME_STEPS")) do
  line = line:gsub("^%s+", ""):gsub("%s+$", "")
  if line ~= "" and not line:match("^#") then
    local op, rest = line:match("^(%S+)%s*(.*)$")
    steps[#steps + 1] = { op = op, arg = rest }
  end
end

local m      = manager.machine
local target = os.getenv("MAME_TARGET")
local cpu    = m.devices[":maincpu"]
local mem    = cpu.spaces["program"]
local kb     = m.natkeyboard
local scr
for _, s in pairs(m.screens) do scr = s break end

L(string.format("start emu %.3f s", m.time:as_double()))

local idx, frame, until_frame = 1, 0, 0
local tape, pcmin, chord = false, nil, nil

local function unescape(s)
  return (s:gsub("\\r", "\r"):gsub("\\s", " "))
end

-- MAME's specnext driver gives its natural keyboard no SYMBOL SHIFT, so it
-- DROPS every symbol-shifted character without a word -- among them the game's
-- , < > ; ? : \ keys. So a lone symbol is typed as the chord itself, on the
-- matrix fields, on both targets: SYMBOL SHIFT + the key that carries it.
local SYM = { [","] = "n", ["."] = "m", [";"] = "o", ['"'] = "p", ["<"] = "r", [">"] = "t",
  [":"] = "z", ["?"] = "c", ["/"] = "v", ["*"] = "b", ["-"] = "j", ["+"] = "k", ["="] = "l",
  ["^"] = "h", ["_"] = "0", ["!"] = "1", ["@"] = "2", ["#"] = "3", ["$"] = "4", ["%"] = "5",
  ["&"] = "6", ["'"] = "7", ["("] = "8", [")"] = "9", ["\\"] = "d", ["|"] = "s", ["["] = "y",
  ["]"] = "u", ["{"] = "f", ["}"] = "g", ["~"] = "a" }
local keyfield = {}          -- "n" -> the matrix field whose legend starts "n "
for _, port in pairs(m.ioport.ports) do
  for name, f in pairs(port.fields) do
    local first = name:match("^(%S+)")
    if name == "SYMBOL SHIFT" then keyfield.sym = f
    elseif first and #first == 1 then keyfield[first] = f end
  end
end
local CHORD_HOLD, CHORD_GAP = 5, 3   -- frames: under both targets' first repeat

-- Row 0 as text. The Next draws the tilemap at 0x6000, 2 B/cell, 80 wide, and
-- the tile id of a text cell IS its ASCII code. The 128K draws ULA pixels, so
-- each of the 32 cells is matched against the ROM font (0x3D00 = ' ').
local function msgline()
  local s = {}
  if target == "zx128" then
    for x = 0, 31 do
      local hit = " "
      for ch = 0, 95 do
        local ok = true
        for r = 0, 7 do
          if mem:read_u8(0x4000 + r * 256 + x) ~= mem:read_u8(0x3D00 + ch * 8 + r) then
            ok = false break
          end
        end
        if ok then hit = string.char(32 + ch) break end
      end
      s[#s + 1] = hit
    end
  else
    for x = 0, 79 do
      local ch = mem:read_u8(0x6000 + x * 2)
      s[#s + 1] = (ch >= 32 and ch < 127) and string.char(ch) or " "
    end
  end
  return (table.concat(s):gsub("%s+$", ""))
end

local function run()
  while idx <= #steps do
    local op, a = steps[idx].op, steps[idx].arg
    idx = idx + 1
    if op == "wait" then until_frame = frame + math.floor(tonumber(a) * 50 + 0.5) return
    elseif op == "frames" then until_frame = frame + tonumber(a) return
    elseif op == "key" then
      local text = unescape(a)
      local base = SYM[text] and keyfield[SYM[text]]
      if base and keyfield.sym then
        keyfield.sym:set_value(1)
        base:set_value(1)
        chord = { keyfield.sym, base, up = frame + CHORD_HOLD }
        until_frame = chord.up + CHORD_GAP
        return
      end
      kb:post(text)
    elseif op == "coded" then kb:post_coded(a)
    elseif op == "play" then m.cassettes[":cassette"]:play()
    elseif op == "waittape" then tape = true return
    elseif op == "waitpc" then pcmin = tonumber(a) return
    elseif op == "load" then
      local err = m.images[":snapshot"]:load(a)
      L("load " .. a .. (err and (" FAILED: " .. tostring(err)) or "") .. " @frame " .. frame)
    elseif op == "peek" then
      local ad, n, label = a:match("^(%d+)%s+(%d+)%s*(.*)$")
      ad, n = tonumber(ad), tonumber(n)
      local t = {}
      for i = 0, n - 1 do t[#t + 1] = string.format("%02X", mem:read_u8(ad + i)) end
      L(string.format("peek %s %04X: %s", label, ad, table.concat(t, " ")))
    elseif op == "poke" then
      local ad, vals = a:match("^(%d+)%s+(.*)$")
      ad = tonumber(ad)
      for v in vals:gmatch("%d+") do mem:write_u8(ad, tonumber(v)) ad = ad + 1 end
    elseif op == "msg" then L("msg " .. msgline())
    elseif op == "snap" then scr:snapshot(a .. ".png") L("snap " .. a .. " @frame " .. frame)
    elseif op == "pc" then L(string.format("pc %04X", cpu.state["PC"].value))
    elseif op == "log" then L(a)
    elseif op == "time" then L(string.format("time frame %d emu %.3f s", frame, m.time:as_double()))
    elseif op == "exit" then L("exit @frame " .. frame) log:close() m:exit() return
    else L("unknown step: " .. op)
    end
  end
end

-- Keep the subscription referenced, or the collector ends the test.
frame_sub = emu.add_machine_frame_notifier(function()
  frame = frame + 1
  if chord and frame >= chord.up then
    chord[1]:clear_value()
    chord[2]:clear_value()
    chord = nil
  end
  if tape then
    local c = m.cassettes[":cassette"]
    if c.position < c.length - 0.01 then return end
    tape = false
    L(string.format("tape end @frame %d", frame))
  end
  if pcmin then
    if cpu.state["PC"].value < pcmin then return end
    pcmin = nil
    L(string.format("pc reached @frame %d (emu %.3f s)", frame, m.time:as_double()))
  end
  if frame >= until_frame then run() end
end)
