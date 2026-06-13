# === NextHack incremental + parallel build ===
#   .\build.ps1           build nexthack.nex, recompiling only changed modules
#   .\build.ps1 -Clean    force a full rebuild of every module
#
# Each .c is compiled separately to a .o (byte-identical to the single-shot
# build.bat -- zcc/SDCC already compile per translation unit), so we can:
#   * skip modules whose .c and headers are untouched  -> fast edits
#   * compile the stale ones in parallel across cores   -> fast clean builds
# Keeps --max-allocs-per-node200000, i.e. NO loss of code quality vs build.bat.
# build.bat stays as the simple single-shot fallback.
param([switch]$Clean)

$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
Set-Location $root

$env:ZCCCFG = (Join-Path $root '..\z88dk\lib\config\')
$env:PATH   = (Join-Path $root '..\z88dk\bin') + ';' + $env:PATH
$zcccfg = $env:ZCCCFG   # captured for the parallel runspaces ($using:)
$zpath  = $env:PATH

$srcs   = 'nexthack','platform','rng','level','monster','item','sfx'
$cflags = @('+zxn','-clib=sdcc_iy','-SO3','--max-allocs-per-node200000')

# Coarse but safe dependency rule: a module is stale if its .o is missing,
# older than its own .c, or older than ANY header (a game.h/platform.h change
# touches many modules, so rebuild them all). Most edits are to one .c -> only
# that module recompiles.
$headerTime = (Get-ChildItem *.h | Measure-Object LastWriteTime -Maximum).Maximum
$todo = $srcs | Where-Object {
    $o = "$_.o"
    $Clean -or
    -not (Test-Path $o) -or
    ((Get-Item $o).LastWriteTime -lt (Get-Item "$_.c").LastWriteTime) -or
    ((Get-Item $o).LastWriteTime -lt $headerTime)
}

$sw = [Diagnostics.Stopwatch]::StartNew()

if ($todo) {
    "Compiling ($($todo.Count)): $($todo -join ', ')"
    $results = $todo | ForEach-Object -ThrottleLimit 8 -Parallel {
        $env:ZCCCFG = $using:zcccfg
        $env:PATH   = $using:zpath
        $log = & zcc $using:cflags -c "$_.c" -o "$_.o" 2>&1
        [pscustomobject]@{ Mod = $_; Code = $LASTEXITCODE; Log = ($log -join "`n") }
    }
    $failed = $results | Where-Object { $_.Code -ne 0 -or -not (Test-Path "$($_.Mod).o") }
    if ($failed) {
        $failed | ForEach-Object { "COMPILE FAILED: $($_.Mod).c`n$($_.Log)" }
        throw "Build aborted: $($failed.Count) module(s) failed to compile."
    }
} else {
    "All objects up to date."
}

# Link stage (~2s): combine the .o files into the .nex and emit the .map.
$objs = $srcs | ForEach-Object { "$_.o" }
& zcc +zxn -subtype=nex -vn -clib=sdcc_iy -m $objs -o nexthack -create-app
$sw.Stop()

if (Test-Path nexthack.nex) {
    "OK: nexthack.nex built in {0:N1}s." -f $sw.Elapsed.TotalSeconds
} else {
    throw "LINK FAILED."
}
