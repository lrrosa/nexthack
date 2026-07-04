# === NextHack incremental + parallel build (banked; nightly z88dk) ===
#   .\build.ps1           build nexthack.nex, recompiling only changed modules
#   .\build.ps1 -Clean    force a full rebuild of every module
#
# The game is code-banked (>64K): hot code + all data resident in 0x8000-0xBFF0,
# cold code banked into PAGE_20/PAGE_22 (the 0xC000 window). That needs the
# nightly z88dk (__banked trampoline) + the banking pragmas (zpragma.inc, and
# mmap.inc auto-appended via CRT_APPEND_MMAP). Each .c compiles separately to a
# .o (banking sections preserved), so we skip untouched modules and compile the
# stale ones in parallel; the link applies the banking pragmas + mmap.
# build.bat is the simple single-shot equivalent.
param([switch]$Clean)

$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
Set-Location $root

$env:ZCCCFG = (Join-Path $root '..\z88dk-latest\lib\config\')
$env:PATH   = (Join-Path $root '..\z88dk-latest\bin') + ';' + $env:PATH
$zcccfg = $env:ZCCCFG   # captured for the parallel runspaces ($using:)
$zpath  = $env:PATH

$srcs   = 'mainentry','nexthack','platform','platform_init','rng','level','levelgen','levelfov','monster','monster_ai','item','sfx','leveltmpl','classes','titlegfx0','titlegfx1','titlegfx2','titlepal','victorygfx0','victorygfx1','victorygfx2','victorypal'
$cflags = @('+zxn','-clib=sdcc_iy','-SO3','--max-allocs-per-node200000','-pragma-include:zpragma.inc')

# Coarse but safe dependency rule: a module is stale if its .o is missing,
# older than its own .c, or older than ANY header (a game.h/platform.h change
# touches many modules, so rebuild them all) or zpragma.inc/mmap.inc. Most edits
# are to one .c -> only that module recompiles.
$depTime = (Get-ChildItem src/*.h, zpragma.inc, mmap.inc | Measure-Object LastWriteTime -Maximum).Maximum
$todo = $srcs | Where-Object {
    $o = "src/$_.o"
    $Clean -or
    -not (Test-Path $o) -or
    ((Get-Item $o).LastWriteTime -lt (Get-Item "src/$_.c").LastWriteTime) -or
    ((Get-Item $o).LastWriteTime -lt $depTime)
}

$sw = [Diagnostics.Stopwatch]::StartNew()

if ($todo) {
    "Compiling ($($todo.Count)): $($todo -join ', ')"
    $results = $todo | ForEach-Object -ThrottleLimit 8 -Parallel {
        $env:ZCCCFG = $using:zcccfg
        $env:PATH   = $using:zpath
        $log = & zcc $using:cflags -c "src/$_.c" -o "src/$_.o" 2>&1
        [pscustomobject]@{ Mod = $_; Code = $LASTEXITCODE; Log = ($log -join "`n") }
    }
    $failed = $results | Where-Object { $_.Code -ne 0 -or -not (Test-Path "src/$($_.Mod).o") }
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
    $code = (Select-String -Path nexthack.map -Pattern '__CODE_END_tail\s+=\s+\$([0-9A-Fa-f]+)').Matches[0].Groups[1].Value
    $bss  = (Select-String -Path nexthack.map -Pattern '__BSS_END_tail\s+=\s+\$([0-9A-Fa-f]+)').Matches[0].Groups[1].Value
    "OK: nexthack.nex built in {0:N1}s.  resident __CODE_END=`${1}  __BSS_END=`${2}" -f $sw.Elapsed.TotalSeconds, $code, $bss
} else {
    throw "LINK FAILED."
}
