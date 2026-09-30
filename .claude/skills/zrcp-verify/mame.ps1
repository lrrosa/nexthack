# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Leonardo Roman da Rosa
#
# mame.ps1 - drive NextHack inside MAME, the second emulator. MAME has no debug
# socket, so a test is a list of steps run in one go by mame.lua (its
# -autoboot_script); see the step vocabulary at the top of mame.lua.
#
#     . "$PSScriptRoot\mame.ps1"
#     $r = Invoke-Mame zx128 ((Get-MameBoot zx128) + (Get-MameNewGame) + @(
#              'peek @hero_x 1', 'key l', 'wait 1', 'peek @hero_x 1', 'msg', 'snap moved'))
#     Get-MamePeek $r hero_x          # every read of that label, in order
#
# Addresses in steps: @sym, @sym+N (from the target's CURRENT .map), 0xHEX or
# decimal. Poke values are decimal. Nothing is written to the MAME folder: cfg,
# nvram, the SD's diff and the snapshots all live under -OutDir.

$script:MamePort = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $PSScriptRoot))
$script:MameDir  = if ($env:NEXTHACK_MAME) { $env:NEXTHACK_MAME } else { 'F:\jogos\emuladores\mame' }
$script:MameSd   = Join-Path $script:MameDir 'roms\specnext_sd\sys2411.chd'
$script:MameLua  = Join-Path $PSScriptRoot 'mame.lua'
$script:MameOut  = Join-Path ([IO.Path]::GetTempPath()) 'nexthack-mame'

function Get-MameSyms([string]$Target) {
    $map = Join-Path $script:MamePort $(if ($Target -eq 'zx128') { 'nexthack128.map' } else { 'nexthack.map' })
    if (-not (Test-Path $map)) { throw "mame: $map missing (build first)" }
    $h = @{}
    foreach ($line in Get-Content $map) {
        if ($line -match '^_(\w+)\s+=\s+\$([0-9A-Fa-f]+)' -and -not $h.ContainsKey($Matches[1])) {
            $h[$Matches[1]] = [Convert]::ToInt32($Matches[2], 16)
        }
    }
    $h
}

function Resolve-MameAddr([string]$a, $syms) {
    if ($a -match '^@(\w+)(?:\+(\d+))?$') {
        if (-not $syms.ContainsKey($Matches[1])) { throw "mame: _$($Matches[1]) not in the map" }
        return $syms[$Matches[1]] + [int]$Matches[2]
    }
    if ($a -match '^0x([0-9A-Fa-f]+)$') { return [Convert]::ToInt32($Matches[1], 16) }
    return [int]$a
}

# Boot to the title. The 128K loads the real .tap in real time (~616 s emulated,
# ~30 s wall headless): Enter picks the 128 menu's Tape Loader. NOTHING may be typed
# until the game runs -- SPACE is BREAK to the ROM's LD-BYTES and silently
# kills the load -- hence waitpc. The Next without -Sd starts the .nex at power
# on (-dump), so the ROM is the TBBlue boot loader: the game logic, the tilemap
# and Layer 2 are real, the font is garbage and there is no esxDOS. With -Sd it
# boots NextZXOS off the card first, then mounts the .nex (UNVERIFIED: the only
# card tried, the 2020 CSpect image, freezes the boot ROM -- see SKILL.md).
function Get-MameBoot([ValidateSet('zx128', 'next')][string]$Target, [switch]$Sd) {
    if ($Target -eq 'zx128') {
        return @('wait 3', 'key \r', 'wait 1', 'play', 'waitpc 0x8000', 'wait 2')
    }
    if (-not $Sd) { return @('waitpc 0x8000', 'wait 3') }
    @('wait 12', "load $(Join-Path $script:MamePort 'nexthack.nex')", 'waitpc 0x8000', 'wait 3')
}

function Get-MameNewGame([char]$Class = 'a') {
    @('key \s', 'wait 2', "key $Class", 'wait 3')
}

function Invoke-Mame {
    param([Parameter(Mandatory)][ValidateSet('zx128', 'next')][string]$Target,
          [Parameter(Mandatory)][string[]]$Steps,
          [string]$Tag = 'run',
          [string]$OutDir = $script:MameOut,
          [switch]$Throttle,        # real speed, e.g. to watch it
          [switch]$Visible,         # open a window (default: headless)
          [int]$MaxSeconds = 0,     # emulated-time cap (0 = 900 on the 128K, 300 on the Next)
          [switch]$Sd,              # Next: boot NextZXOS off the SD card first
          [switch]$FreshSd,         # drop the SD's diff (saves made by earlier runs)
          [string[]]$ExtraArgs = @())
    $syms = Get-MameSyms $Target
    $lines = foreach ($s in ($Steps + 'exit')) {
        if ($s -match '^(peek|poke)\s+(\S+)\s*(.*)$') {
            $op, $addr, $rest = $Matches[1], $Matches[2], $Matches[3]
            $ad = Resolve-MameAddr $addr $syms
            if ($op -eq 'peek') {             # peek <addr> [n] [label]
                $p = @($rest -split '\s+' | Where-Object { $_ })
                $n = if ($p.Count -gt 0) { $p[0] } else { 1 }
                $label = if ($p.Count -gt 1) { $p[1] } else { $addr.TrimStart('@') }
                "peek $ad $n $label"
            } else { "poke $ad $rest" }
        } elseif ($s -match '^waitpc\s+(\S+)$') {
            "waitpc $(Resolve-MameAddr $Matches[1] $syms)"
        } else { $s }
    }
    $dir = Join-Path $OutDir $Tag
    New-Item -ItemType Directory -Force $dir | Out-Null
    Get-ChildItem $dir -Filter *.png -ErrorAction SilentlyContinue | Remove-Item
    $diff = Join-Path $OutDir 'diff'
    if ($FreshSd -and (Test-Path $diff)) { Remove-Item $diff -Recurse -Force }

    $stepFile = Join-Path $dir 'steps.txt'
    $logFile  = Join-Path $dir 'log.txt'
    $lines | Set-Content $stepFile
    $env:MAME_STEPS = $stepFile
    $env:MAME_LOG = $logFile
    $env:MAME_TARGET = $Target

    # Headless by default, and deaf to the host: a MAME window takes the focus
    # when it opens, and whatever the user then types goes into the emulated
    # keyboard -- that made identical scripts produce different worlds.
    $opts = @('-rompath', (Join-Path $script:MameDir 'roms'), '-sound', 'none',
              '-keyboardprovider', 'none', '-mouseprovider', 'none', '-joystickprovider', 'none',
              '-skip_gameinfo', '-snapshot_directory', $dir,
              '-cfg_directory', (Join-Path $OutDir 'cfg'), '-nvram_directory', (Join-Path $OutDir 'nvram'),
              '-diff_directory', $diff, '-autoboot_script', $script:MameLua)
    if (-not $Throttle) { $opts += '-nothrottle' }
    $opts += $(if ($Visible) { @('-window') } else { @('-video', 'none') })
    # A headless run whose script stalls (a waitpc never met) would otherwise
    # run on unseen forever; the tape alone is ~616 s of emulated time.
    if ($MaxSeconds -le 0) { $MaxSeconds = if ($Target -eq 'zx128') { 900 } else { 300 } }
    $opts += @('-seconds_to_run', $MaxSeconds)
    $opts += $ExtraArgs
    if ($Target -eq 'zx128') {
        $media = @('spec128', '-cass', (Join-Path $script:MamePort 'nexthack128.tap'))
    } elseif ($Sd) {
        if (-not (Test-Path $script:MameSd)) { throw "mame: no NextZXOS SD at $script:MameSd" }
        $media = @('specnext_ks2', '-hard1', $script:MameSd)
    } else {
        $media = @('specnext_ks2', '-dump', (Join-Path $script:MamePort 'nexthack.nex'))
    }
    Push-Location $script:MameDir
    try { & .\mame.exe @media @opts *>&1 | Out-File (Join-Path $dir 'mame.out') } finally { Pop-Location }
    $log = if (Test-Path $logFile) { Get-Content $logFile } else { @() }
    if (-not ($log -match '^exit')) { Write-Warning "mame: the run did not reach its end (see $logFile)" }
    [pscustomobject]@{ Log = $log; Dir = $dir }
}

# Every read of a label, oldest first, as byte arrays.
function Get-MamePeek($Run, [string]$Label) {
    foreach ($l in $Run.Log) {
        if ($l -match "^peek $([regex]::Escape($Label)) [0-9A-F]{4}: (.*)$") {
            ,@($Matches[1] -split ' ' | ForEach-Object { [Convert]::ToInt32($_, 16) })
        }
    }
}

function Get-MameMsg($Run) { $Run.Log | Where-Object { $_ -like 'msg *' } | ForEach-Object { $_.Substring(4) } }
