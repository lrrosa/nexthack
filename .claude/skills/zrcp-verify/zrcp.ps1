# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Leonardo Roman da Rosa
#
# zrcp.ps1 - reusable harness for driving NextHack inside ZEsarUX over ZRCP
# (its TCP debug protocol on :10000). Dot-source it, then write only the part
# of the test that is actually about the feature:
#
#     . "$PSScriptRoot\zrcp.ps1"          # or the skill's absolute path
#     Start-Emu next                       # launches + waits + connects
#     Start-NewGame
#     $hx = Get-Byte (Sym hero_x)
#     Send-Key 108                         # 'l'
#     Get-MsgLine
#
# Function names deliberately avoid PowerShell aliases (R = Invoke-History,
# rd = Remove-Item have both bitten this project). Verb-Noun names are safe.

$script:ZrcpClient = $null
$script:ZrcpStream = $null
$script:ZrcpTarget = 'next'
$script:ZrcpSymbols = $null

$script:PortDir = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $PSScriptRoot))
$script:EmuExe  = Join-Path (Split-Path -Parent $script:PortDir) 'ZEsarUX\zesarux.exe'

# --------------------------------------------------------------- connection

function Connect-Zrcp {
    param([int]$TimeoutSec = 30)
    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    while ((Get-Date) -lt $deadline) {
        try {
            $script:ZrcpClient = New-Object System.Net.Sockets.TcpClient('127.0.0.1', 10000)
            $script:ZrcpStream = $script:ZrcpClient.GetStream()
            $null = Send-Zrcp ''      # drain the banner
            return $true
        } catch {
            Start-Sleep -Milliseconds 700
        }
    }
    throw 'Connect-Zrcp: no ZEsarUX listening on :10000 (did it die? Get-Process zesarux)'
}

function Disconnect-Zrcp {
    if ($script:ZrcpClient) { $script:ZrcpClient.Close() }
    $script:ZrcpClient = $null
    $script:ZrcpStream = $null
}

function Send-Zrcp {
    param([Parameter(Mandatory)][AllowEmptyString()][string]$Command,
          [int]$SettleMs = 320)
    $b = [Text.Encoding]::ASCII.GetBytes($Command + "`n")
    $script:ZrcpStream.Write($b, 0, $b.Length)
    Start-Sleep -Milliseconds $SettleMs
    $buf = New-Object byte[] 65536
    $out = ''
    while ($script:ZrcpStream.DataAvailable) {
        $n = $script:ZrcpStream.Read($buf, 0, $buf.Length)
        $out += [Text.Encoding]::ASCII.GetString($buf, 0, $n)
        Start-Sleep -Milliseconds 60
    }
    return $out
}

# ------------------------------------------------------------------ emulator
# NEVER use ZRCP's `set-machine` to switch targets: it kills the v13 pthreads
# build outright (the process dies, not just the connection). Relaunch instead.

function Stop-Emu {
    Get-Process | Where-Object { $_.ProcessName -match 'zesarux' } |
        Stop-Process -Force -ErrorAction SilentlyContinue
    Disconnect-Zrcp
    Start-Sleep -Seconds 2
}

function Start-Emu {
    param([ValidateSet('next', 'zx128')][string]$Target = 'next',
          [switch]$KeepSave)
    Stop-Emu
    $script:ZrcpTarget = $Target
    $script:ZrcpSymbols = $null

    # A boot CONSUMES and deletes nexthack.sav. Stash it unless asked not to.
    $sav = Join-Path $script:PortDir 'nexthack.sav'
    if ((Test-Path $sav) -and -not $KeepSave) {
        Move-Item $sav "$sav.bak" -Force
        Write-Host "zrcp: stashed nexthack.sav -> nexthack.sav.bak (a boot would eat it)"
    }

    $common = @('--enable-remoteprotocol', '--nowelcomemessage', '--quickexit')
    if ($Target -eq 'next') {
        $args = @((Join-Path $script:PortDir 'nexthack.nex')) + $common
    } else {
        # --noconfigfile MUST be first: the shared .zesaruxrc that a Next run
        # leaves behind forces TBBlue + autoloadsnap and reloads the last .nex
        # over the tape. Never --enable-divmmc-paging (hijacks the tape boot).
        $args = @('--noconfigfile', '--machine', '128k',
                  '--enable-divmmc-ports', '--enable-esxdos-handler',
                  '--esxdos-root-dir', "`"$script:PortDir`"",
                  '--diviface-ram-size', '128',
                  '--tape', "`"$(Join-Path $script:PortDir 'nexthack128.tap')`"") + $common
    }
    Start-Process -FilePath $script:EmuExe `
                  -WorkingDirectory (Split-Path -Parent $script:EmuExe) `
                  -ArgumentList $args
    Start-Sleep -Seconds 10
    Connect-Zrcp | Out-Null
    if ($Target -eq 'zx128') {
        Write-Host 'zrcp: waiting for the tape autoload...'
        Start-Sleep -Seconds 8
    }
    Write-Host "zrcp: $Target up"
}

# ------------------------------------------------------------------- symbols
# Every rebuild shifts addresses. Always resolve from the CURRENT .map -- a
# stale hard-coded address reads garbage that looks like a game-state bug.

function Sym {
    param([Parameter(Mandatory)][string]$Name)
    if (-not $script:ZrcpSymbols) {
        $mapfile = if ($script:ZrcpTarget -eq 'next') { 'nexthack.map' } else { 'nexthack128.map' }
        $path = Join-Path $script:PortDir $mapfile
        if (-not (Test-Path $path)) { throw "Sym: $mapfile missing (build first)" }
        $script:ZrcpSymbols = @{}
        foreach ($line in Get-Content $path) {
            if ($line -match '^(_\w+)\s+=\s+\$([0-9A-Fa-f]+)') {
                if (-not $script:ZrcpSymbols.ContainsKey($Matches[1])) {
                    $script:ZrcpSymbols[$Matches[1]] = [Convert]::ToInt32($Matches[2], 16)
                }
            }
        }
    }
    $key = if ($Name.StartsWith('_')) { $Name } else { "_$Name" }
    if (-not $script:ZrcpSymbols.ContainsKey($key)) { throw "Sym: $key not in the map" }
    return $script:ZrcpSymbols[$key]
}

# --------------------------------------------------------------------- memory
# read-memory parses its address as DECIMAL and prints HEX; write-memory parses
# BOTH the address and the byte values as DECIMAL. Never interpolate arithmetic
# into the command string -- pre-compute into a variable.

function Get-Bytes {
    param([Parameter(Mandatory)][int]$Addr, [int]$Length = 1, [int]$Retries = 4)
    for ($try = 0; $try -lt $Retries; $try++) {
        $r = Send-Zrcp "read-memory $Addr $Length"
        $line = $r -split "`n" | Where-Object { $_ -match '^[0-9A-Fa-f]+\s*$' } | Select-Object -First 1
        if ($line) {
            $hex = $line.Trim()
            $vals = @()
            for ($i = 0; $i + 1 -lt $hex.Length; $i += 2) {
                $vals += [Convert]::ToInt32($hex.Substring($i, 2), 16)
            }
            return $vals
        }
        Start-Sleep -Milliseconds 400
    }
    throw "Get-Bytes: no reply for $Length B at $Addr"
}

function Get-Byte  { param([int]$Addr) (Get-Bytes $Addr 1)[0] }
function Get-Word  { param([int]$Addr) $v = Get-Bytes $Addr 2; $v[0] + 256 * $v[1] }

function Set-Bytes {
    param([Parameter(Mandatory)][int]$Addr, [Parameter(Mandatory)][int[]]$Values)
    $null = Send-Zrcp "write-memory $Addr $($Values -join ' ')"
}

function Set-Word { param([int]$Addr, [int]$Value)
    Set-Bytes $Addr @(($Value -band 0xFF), (($Value -shr 8) -band 0xFF)) }

# ---------------------------------------------------------------------- input

function Send-Key {
    param([Parameter(Mandatory)][int]$Ascii, [int]$HoldMs = 160, [int]$SettleMs = 800)
    $null = Send-Zrcp "send-keys-ascii $HoldMs $Ascii"
    Start-Sleep -Milliseconds $SettleMs
}

function Start-NewGame {
    param([char]$Class = 'a')
    Send-Key 32 -SettleMs 2500          # dismiss the title
    Send-Key ([int][char]$Class) -SettleMs 2500
}

# ---------------------------------------------------------------- the screen
# The render target differs per platform: the Next draws to the hardware
# tilemap at 0x6000 (2 B/cell: tile id + attr, 80 wide); the 128K to the ULA
# bitmap at 0x4000 (+ attributes 0x5800), so text there must be decoded by
# matching each rendered 8-byte cell against the ROM font at 0x3C00.

function Get-TileCell {
    # Next only: returns @(tile, attr) for a MAP cell (OX=0, OY=1).
    param([int]$MapX, [int]$MapY)
    Get-Bytes (24576 + ((($MapY + 1) * 80 + $MapX) * 2)) 2
}

function Get-ShadowTile {
    # 128K only: what draw_map last painted at a map cell, via VIEW_SHADOW.
    param([int]$MapX, [int]$MapY)
    $vx = Get-Byte (Sym vx_origin)
    (Get-Bytes (24576 + ((($MapY * 32) + ($MapX - $vx)) * 2)) 2)[0]
}

function Get-MsgLine {
    # Row 0 (the message line) as text, on either target.
    if ($script:ZrcpTarget -eq 'next') {
        $row = Get-Bytes 24576 160
        $s = ''
        for ($i = 0; $i -lt $row.Count; $i += 2) {
            $ch = $row[$i]
            if ($ch -ge 32 -and $ch -lt 127) { $s += [char]$ch }
        }
        return $s.TrimEnd()
    }
    if (-not $script:ZrcpFont) { $script:ZrcpFont = Get-Bytes 15616 768 }  # 0x3D00, ' '..
    $rows = @()
    for ($r = 0; $r -lt 8; $r++) { $rows += ,(Get-Bytes (16384 + $r * 256) 32) }
    $s = ''
    for ($x = 0; $x -lt 32; $x++) {
        $hit = ' '
        for ($ch = 0; $ch -lt 96; $ch++) {
            $ok = $true
            for ($r = 0; $r -lt 8; $r++) {
                if ($rows[$r][$x] -ne $script:ZrcpFont[$ch * 8 + $r]) { $ok = $false; break }
            }
            if ($ok) { $hit = [char](32 + $ch); break }
        }
        $s += $hit
    }
    return $s.TrimEnd()
}

# ------------------------------------------------------------------ game aids

function Get-Hero { @((Get-Byte (Sym hero_x)), (Get-Byte (Sym hero_y))) }

function Set-Hero {
    param([int]$X, [int]$Y)
    Set-Bytes (Sym hero_x) @($X, 0, $Y, 0)     # both are 16-bit, little-endian
}

function Invoke-Descend {
    # Teleport onto this level's down-stairs and take them (the agent cannot
    # walk the hero across the map). Works for 'v' (mines) too via Enter.
    Set-Hero (Get-Byte (Sym dn_x)) (Get-Byte (Sym dn_y))
    Send-Key 62 -SettleMs 2000                  # '>'
    Get-Word (Sym dlvl)
}

function Set-Tank {
    # Survive a long test: a poked hero still dies to wandering monsters.
    param([int]$Hp = 200)
    Set-Bytes (Sym php) @($Hp)
    Set-Bytes (Sym pmaxhp) @($Hp)
}

function Save-EmuScreenshot {
    # PrintWindow with PW_RENDERFULLCONTENT grabs the ZEsarUX window even when
    # it is occluded; a plain CopyFromScreen grabs whatever overlaps it.
    param([Parameter(Mandatory)][string]$Path)
    Add-Type -ReferencedAssemblies System.Drawing -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public class ZrcpShot {
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr dc, uint f);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
}
'@ -ErrorAction SilentlyContinue
    $p = Get-Process | Where-Object { $_.ProcessName -match 'zesarux' } | Select-Object -First 1
    $rect = New-Object ZrcpShot+RECT
    $null = [ZrcpShot]::GetWindowRect($p.MainWindowHandle, [ref]$rect)
    $bmp = New-Object System.Drawing.Bitmap ($rect.R - $rect.L), ($rect.B - $rect.T)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $hdc = $g.GetHdc()
    $null = [ZrcpShot]::PrintWindow($p.MainWindowHandle, $hdc, 2)
    $g.ReleaseHdc($hdc); $g.Dispose()
    $bmp.Save($Path); $bmp.Dispose()
    Write-Host "zrcp: saved $Path"
}
