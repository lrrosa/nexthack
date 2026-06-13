# === Banking build (restructure branch) ===
# Nightly z88dk (..\z88dk-latest) + banking pragmas (zpragma.inc/mmap.inc).
# Produces the canonical nexthack.nex so run.bat / run-sd.bat work directly.
# This is the branch's build; build.bat / build.ps1 (pinned, 64K) will NOT
# compile this source (the pinned toolchain lacks __banked) -- use this script.
#
# Resident half lives in 0x8000-0xBFF0; cold code is banked into PAGE_20/PAGE_22
# (0xC000 window). Reports resident __CODE_END/__BSS_END (must stay below the
# 0xBDF0 stack floor).
param([switch]$Clean)
$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
Set-Location $root

$nightly = (Resolve-Path "$root\..\z88dk-latest").Path
$env:ZCCCFG = "$nightly\lib\config\"
$env:PATH   = "$nightly\bin;$env:PATH"

$srcs = 'mainentry.c','nexthack.c','platform.c','platform_init.c','rng.c','level.c','levelgen.c','levelfov.c','monster.c','monster_ai.c','item.c','sfx.c'
Remove-Item nexthack.nex,nexthack.map -Force -ErrorAction SilentlyContinue

$sw = [Diagnostics.Stopwatch]::StartNew()
zcc +zxn -subtype=nex -vn -SO3 -clib=sdcc_iy --max-allocs-per-node200000 `
    -startup=1 -pragma-include:zpragma.inc -m $srcs -o nexthack -create-app
$sw.Stop()

if (Test-Path nexthack.nex) {
    $code = (Select-String -Path nexthack.map -Pattern '__CODE_END_tail\s+=\s+\$([0-9A-Fa-f]+)').Matches[0].Groups[1].Value
    $bss  = (Select-String -Path nexthack.map -Pattern '__BSS_END_tail\s+=\s+\$([0-9A-Fa-f]+)').Matches[0].Groups[1].Value
    "OK: nexthack.nex built in {0:N0}s.  resident __CODE_END=`${1}  __BSS_END=`${2}  (stack floor 0xBDF0)" -f $sw.Elapsed.TotalSeconds, $code, $bss
} else {
    "BUILD FAILED."
}
