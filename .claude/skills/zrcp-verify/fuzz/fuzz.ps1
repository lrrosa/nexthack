# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Leonardo Roman da Rosa
#
# fuzz.ps1 - run the MAME fuzzer (fuzz.lua) on one target, headless:
#
#     & .claude\skills\zrcp-verify\fuzz\fuzz.ps1 zx128 -Seed 7 -Frames 30000
#     & .claude\skills\zrcp-verify\fuzz\fuzz.ps1 next -Scan -Seed 3 -Tag scan3
#
# It writes the config from the CURRENT build (fuzzcfg.py), copies the .tap/.nex
# into the run's folder (two MAME instances cannot share one image) and plays
# -Frames frames (50 a second) after the class pick. The run's folder,
# <OutDir>\<Tag> (default %TEMP%\nexthack-fuzz\fz-<target>-<seed>), keeps
# log.txt, log.txt.cover (for cover.py), cfg.lua, mame.out and a snapshot per
# new kind of violation. -Dump (or $env:FUZZ_DUMP=1) also logs the last 40 keys
# and the map at each kind's first violation. MAME is $env:NEXTHACK_MAME, as
# for mame.ps1; -PortDir is the tree whose build to fuzz (default: this repo).

param([Parameter(Position = 0)][ValidateSet('zx128', 'next')][string]$Target = 'zx128',
      [int]$Seed = 1,            # pins the world, the class's dice and every key
      [int]$Frames = 30000,      # of play, after the class pick
      [string]$Class = 'a',      # the class picker's letter
      [int]$Stairs = 70,         # take the stairs every this many actions
      [switch]$Scan,             # visit random levels of random worlds instead
      [switch]$Dump,
      [string]$PortDir = '',
      [string]$OutDir = '',
      [string]$Tag = '')

. (Join-Path (Split-Path -Parent $PSScriptRoot) 'mame.ps1')    # $script:MameDir, $script:MamePort
if (-not $PortDir) { $PortDir = $script:MamePort }
$PortDir = (Resolve-Path $PortDir).Path
if (-not $OutDir) { $OutDir = Join-Path ([IO.Path]::GetTempPath()) 'nexthack-fuzz' }
$mame = $script:MameDir
if (-not (Test-Path (Join-Path $mame 'mame.exe'))) { throw "fuzz: no mame.exe in $mame (set `$env:NEXTHACK_MAME)" }
$bin = if ($Target -eq 'zx128') { 'nexthack128.tap' } else { 'nexthack.nex' }
if (-not (Test-Path (Join-Path $PortDir $bin))) { throw "fuzz: $PortDir\$bin missing (build first, or -PortDir)" }
if (-not $Tag) { $Tag = "fz-$Target-$Seed" + $(if ($Scan) { '-scan' } else { '' }) }

$dir = Join-Path $OutDir $Tag
if (Test-Path $dir) { Remove-Item $dir -Recurse -Force }
New-Item -ItemType Directory -Force $dir | Out-Null
$cfg = Join-Path $dir 'cfg.lua'
& python (Join-Path $PSScriptRoot 'fuzzcfg.py') $Target $PortDir $cfg 2>&1 | Out-File (Join-Path $dir 'cfg.txt')
if ($LASTEXITCODE -ne 0) { throw "fuzz: fuzzcfg.py failed: $(Get-Content (Join-Path $dir 'cfg.txt') -Raw)" }
# MAME opens a tape read-write: give every run its own copy
Copy-Item (Join-Path $PortDir $bin) (Join-Path $dir $bin)

$log = Join-Path $dir 'log.txt'
$env:FUZZ_CFG = $cfg; $env:MAME_LOG = $log
$env:FUZZ_SEED = $Seed; $env:FUZZ_FRAMES = $Frames; $env:FUZZ_CLASS = $Class; $env:FUZZ_STAIRS = $Stairs
$env:FUZZ_SCAN = $(if ($Scan) { '1' } else { '0' })
$oldDump = $env:FUZZ_DUMP
if ($Dump) { $env:FUZZ_DUMP = '1' }
# emulated-time cap: the play, plus the 128K's ~616 s tape load
$secs = [int]($Frames / 50) + $(if ($Target -eq 'zx128') { 800 } else { 60 })
$opts = @('-rompath', (Join-Path $mame 'roms'), '-sound', 'none', '-video', 'none',
          '-keyboardprovider', 'none', '-mouseprovider', 'none', '-joystickprovider', 'none',
          '-skip_gameinfo', '-nothrottle', '-debug', '-debugger', 'none',
          '-snapshot_directory', $dir, '-cfg_directory', (Join-Path $dir 'cfg'),
          '-nvram_directory', (Join-Path $dir 'nv'), '-diff_directory', (Join-Path $dir 'diff'),
          '-seconds_to_run', $secs, '-autoboot_script', (Join-Path $PSScriptRoot 'fuzz.lua'))
$media = if ($Target -eq 'zx128') { @('spec128', '-cass', (Join-Path $dir $bin)) } else { @('specnext_ks2', '-dump', (Join-Path $dir $bin)) }
Push-Location $mame
try { & .\mame.exe @media @opts *>&1 | Out-File (Join-Path $dir 'mame.out') }
finally { Pop-Location; $env:FUZZ_DUMP = $oldDump }

if (-not (Test-Path $log)) { throw "fuzz: no log -- MAME did not run the script (see $dir\mame.out)" }
$lines = Get-Content $log
$lines | Where-Object { $_ -notmatch '^A ' }
# the verdict: a run counts only if it reached its SUMMARY and the control fired
$viol = @($lines | Where-Object { $_ -match '^(VIOLATION|HIT) ' }).Count
if (-not ($lines -match '^SUMMARY ')) { Write-Warning "fuzz: $Tag did not reach its SUMMARY (see $dir\mame.out)" }
elseif (-not ($lines -match '^WATCH \d+x WP control')) { Write-Warning "fuzz: $Tag's control watchpoint never fired: the memory guards proved nothing" }
"fuzz: $Tag -- $viol violation line(s); log $log"
