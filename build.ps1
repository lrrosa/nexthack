# === NextHack incremental + parallel build (banked; nightly z88dk) ===
#   .\build.ps1           build nexthack.nex, recompiling only changed modules
#   .\build.ps1 -Clean    force a full rebuild of every module
#
# The game is code-banked (>64K): hot code + all data resident in 0x8000-0xBFF0,
# cold code banked into PAGE_20/22/26/28 (the 0xC000 window). That needs the
# nightly z88dk (__banked trampoline) + the banking pragmas (zpragma.inc, and
# mmap.inc auto-appended via CRT_APPEND_MMAP). Each .c compiles separately to a
# .o (banking sections preserved), so we skip untouched modules and compile the
# stale ones in parallel; the link applies the banking pragmas + mmap.
#
# WHICH bank each module lands in comes from banks.json, passed to zcc as
# --codeseg/--constseg (the sources carry no #pragma codeseg -- see banks.ps1).
# Moving a module is one edit there, and only that module recompiles.
# build.bat is the simple fallback (it forwards the full build here).
param([switch]$Clean)

$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
Set-Location $root

$env:ZCCCFG = (Join-Path $root '..\z88dk\lib\config\')
$env:PATH   = (Join-Path $root '..\z88dk\bin') + ';' + $env:PATH
$zcccfg = $env:ZCCCFG   # captured for the parallel runspaces ($using:)
$zpath  = $env:PATH

$srcs   = 'mainentry','nexthack','platform','platform_init','rng','level','levelgen','levelfov','monster','monster_ai','monster_spawn','item','sfx','music','leveltmpl','classes','spells','titlegfx0','titlegfx1','titlegfx2','titlepal','victorygfx0','victorygfx1','victorygfx2','victorypal'
$cflags = @('+zxn','-clib=sdcc_iy','-SO3','--max-allocs-per-node200000','-pragma-include:zpragma.inc')

# Bank assignment (banks.json -> --codeseg/--constseg). This also validates that
# every module is declared and that the co-located groups share a bank, and it
# throws before a single module is compiled if either is wrong.
. (Join-Path $root 'banks.ps1')
$bank = Import-BankManifest -Target next -Modules $srcs
$bankFlags = @{}; $srcs | ForEach-Object { $bankFlags[$_] = $bank[$_].Flags }

# Coarse but safe dependency rule: a module is stale if its .o is missing,
# older than its own .c, or older than ANY header (a game.h/platform.h change
# touches many modules, so rebuild them all) or zpragma.inc/mmap.inc. Most edits
# are to one .c -> only that module recompiles.
# The .seg stamp next to the .o records the bank flags it was built with, so
# re-banking a module in banks.json rebuilds exactly that module.
$depTime = (Get-ChildItem src/*.h, zpragma.inc, mmap.inc | Measure-Object LastWriteTime -Maximum).Maximum
$todo = $srcs | Where-Object {
    $o = "src/$_.o"; $seg = "src/$_.seg"
    $Clean -or
    -not (Test-Path $o) -or
    -not (Test-Path $seg) -or
    ((Get-Content $seg -Raw) -ne $bank[$_].Stamp) -or
    ((Get-Item $o).LastWriteTime -lt (Get-Item "src/$_.c").LastWriteTime) -or
    ((Get-Item $o).LastWriteTime -lt $depTime)
}

$sw = [Diagnostics.Stopwatch]::StartNew()

if ($todo) {
    "Compiling ($($todo.Count)): $($todo -join ', ')"
    $results = $todo | ForEach-Object -ThrottleLimit 8 -Parallel {
        $env:ZCCCFG = $using:zcccfg
        $env:PATH   = $using:zpath
        $zargs = @($using:cflags) + ($using:bankFlags)[$_] + @('-c', "src/$_.c", '-o', "src/$_.o")
        $log = & zcc $zargs 2>&1
        [pscustomobject]@{ Mod = $_; Code = $LASTEXITCODE; Log = ($log -join "`n") }
    }
    $failed = $results | Where-Object { $_.Code -ne 0 -or -not (Test-Path "src/$($_.Mod).o") }
    # Stamp only what actually built, so a failed module stays stale next time.
    $results | Where-Object { $_ -notin $failed } |
        ForEach-Object { Set-Content "src/$($_.Mod).seg" $bank[$_.Mod].Stamp -NoNewline }
    if ($failed) {
        $failed | ForEach-Object { "COMPILE FAILED: src/$($_.Mod).c`n$($_.Log)" }
        throw "Build aborted: $($failed.Count) module(s) failed to compile."
    }
} else {
    "All objects up to date."
}

# Link stage: combine the .o files into the .nex (+ .map), applying the banking
# pragmas (REGISTER_SP/CRT_APPEND_MMAP/CLIB_BANKING_SEGMENT) and the page mmap.
$objs = $srcs | ForEach-Object { "src/$_.o" }
& zcc +zxn -subtype=nex -vn -clib=sdcc_iy -startup=1 -pragma-include:zpragma.inc -m $objs -o nexthack -create-app
$sw.Stop()

if (Test-Path nexthack.nex) {
    # A banked page section can silently overflow its 16 KB window: the linker
    # emits it anyway and the tail is lost when the bank is loaded (the 128K
    # shipped a template-level crash exactly this way). Refuse oversized pages.
    $fat = Get-ChildItem "nexthack_PAGE_*.bin" -ErrorAction SilentlyContinue |
           Where-Object { $_.Length -gt 16384 }
    if ($fat) {
        $fat | ForEach-Object { "PAGE OVERFLOW: $($_.Name) is $($_.Length) bytes (16384 max)" }
        throw "Build aborted: a banked page overflowed its 16 KB window."
    }
    # The guard above only sees a bank that outgrew its window. bankmap.py also
    # catches what nothing else does: two Bank-5 tenants overlapping (a grown
    # fov_pool swallowed PREV_VIS for two releases) and the Layer 2 palettes
    # spilling out of bank 11 (scrambles the title, invisible to ZRCP). Silent
    # when clean; the report only prints if something is wrong.
    # Python is optional here (the Next build otherwise needs none) -- if it is
    # missing we warn rather than block the build.
    if (Get-Command python -ErrorAction SilentlyContinue) {
        $bm = & python tools/bankmap.py next --check 2>&1
        if ($LASTEXITCODE -ne 0) {
            $bm
            throw "Build aborted: memory-map problem (see .claude/skills/bank-budget)."
        }
    } else {
        "NOTE: python not found -- skipped the bankmap memory-map check."
    }
    $code = (Select-String -Path nexthack.map -Pattern '__CODE_END_tail\s+=\s+\$([0-9A-Fa-f]+)').Matches[0].Groups[1].Value
    $bss  = (Select-String -Path nexthack.map -Pattern '__BSS_END_tail\s+=\s+\$([0-9A-Fa-f]+)').Matches[0].Groups[1].Value
    "OK: nexthack.nex built in {0:N1}s.  resident __CODE_END=`${1}  __BSS_END=`${2}" -f $sw.Elapsed.TotalSeconds, $code, $bss
} else {
    throw "LINK FAILED."
}
