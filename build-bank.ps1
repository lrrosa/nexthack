# === EXPERIMENTAL banking build (restructure Stage 2+) ===
# Nightly z88dk + banking pragmas. Does NOT touch the working build.bat /
# build.ps1 (pinned, 64K). Output: nexthack_bank.nex. While the resident half
# is still > 16 KB this binary is NOT bootable (stack lands inside code) -- it
# is built only to read the .map and watch resident code shrink as cold modules
# move to the banked codeseg. Boot-test only once resident fits below 0xC000.
param([switch]$Clean)
$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
Set-Location $root

$nightly = (Resolve-Path "$root\..\z88dk-latest").Path
$env:ZCCCFG = "$nightly\lib\config\"
$env:PATH   = "$nightly\bin;$env:PATH"

$srcs = 'mainentry.c','nexthack.c','platform.c','rng.c','level.c','levelgen.c','monster.c','item.c','sfx.c'
Remove-Item nexthack_bank.* -Force -ErrorAction SilentlyContinue

$sw = [Diagnostics.Stopwatch]::StartNew()
zcc +zxn -subtype=nex -vn -SO3 -clib=sdcc_iy --max-allocs-per-node200000 `
    -startup=1 -pragma-include:zpragma.inc -m $srcs -o nexthack_bank -create-app
$sw.Stop()

if (Test-Path nexthack_bank.nex) {
    $code = (Select-String -Path nexthack_bank.map -Pattern '__CODE_END_tail\s+=\s+\$([0-9A-Fa-f]+)').Matches[0].Groups[1].Value
    $bss  = (Select-String -Path nexthack_bank.map -Pattern '__BSS_END_tail\s+=\s+\$([0-9A-Fa-f]+)').Matches[0].Groups[1].Value
    "OK in {0:N0}s.  resident __CODE_END=`${1}  __BSS_END=`${2}" -f $sw.Elapsed.TotalSeconds, $code, $bss
} else {
    "BUILD FAILED."
}
