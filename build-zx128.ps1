# === NextHack ZX Spectrum 128K build (branch zx128-port) ===
#   .\build-zx128.ps1            compile + link -> nexthack128.tap (+ .sna for testing)
#   .\build-zx128.ps1 -Clean     force a full rebuild
#   .\build-zx128.ps1 -CompileOnly   compile every module, report pass/fail (no link)
#
# Classic +zx target, 128K code-banking via port 0x7FFD (see memory
# zx128-banking-recipe): -clib=sdcc_iy (same codegen as the Next build), the
# vendored banked_call.asm trampoline, cold modules in BANK_N sections ORG'd by
# zpragma-zx128.inc. The 96 KB of Next Layer 2 images are dropped; title/victory
# become small SCRs (Phase 2). Objects go to obj-zx128/ with the .o suffix (zcc
# only recognises .o as a linkable object) so they never clash with the Next
# build's src/*.o.
param([switch]$Clean, [switch]$CompileOnly)

$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
Set-Location $root

$env:ZCCCFG = (Join-Path $root '..\z88dk-latest\lib\config\')
$env:PATH   = (Join-Path $root '..\z88dk-latest\bin') + ';' + $env:PATH

$obj = Join-Path $root 'obj-zx128'
if (-not (Test-Path $obj)) { New-Item -ItemType Directory $obj | Out-Null }

# Source modules (drop the 8 Next Layer 2 image modules). banked_call.asm is the
# vendored 0x7FFD trampoline.
$csrcs = 'mainentry','nexthack','platform','platform_init','rng','level','levelgen','levelfov','monster','monster_ai','item','sfx','leveltmpl','classes','scr','title_scr','victory_scr'
$asrcs = 'banked_call','esxdetect','puttile_asm'
$cflags = @('+zx','-clib=sdcc_iy','-SO3','--max-allocs-per-node200000','-pragma-include:zpragma-zx128.inc')

$sw = [Diagnostics.Stopwatch]::StartNew()
$fail = @()

# Incremental: a module is stale if its .o is missing, older than its .c, or
# older than ANY header / zpragma-zx128.inc (a shared-header change rebuilds all).
$depTime = (Get-ChildItem src/*.h, zpragma-zx128.inc | Measure-Object LastWriteTime -Maximum).Maximum
function Stale($src, $o) {
    if ($Clean -or -not (Test-Path $o)) { return $true }
    $ot = (Get-Item $o).LastWriteTime
    return (($ot -lt (Get-Item $src).LastWriteTime) -or ($ot -lt $depTime))
}

foreach ($m in $csrcs) {
    $o = "obj-zx128/$m.o"
    if (Stale "src/$m.c" $o) {
        $log = & zcc $cflags -c "src/$m.c" -o $o 2>&1
        if ($LASTEXITCODE -ne 0 -or -not (Test-Path $o)) {
            $fail += [pscustomobject]@{ Mod = "$m.c"; Log = ($log -join "`n") }
        } else { "  ok  $m.c" }
    } else { "  --  $m.c (up to date)" }
}
foreach ($m in $asrcs) {
    $o = "obj-zx128/$m.o"
    if (Stale "src/$m.asm" $o) {
        $log = & zcc $cflags -c "src/$m.asm" -o $o 2>&1
        if ($LASTEXITCODE -ne 0 -or -not (Test-Path $o)) {
            $fail += [pscustomobject]@{ Mod = "$m.asm"; Log = ($log -join "`n") }
        } else { "  ok  $m.asm" }
    } else { "  --  $m.asm (up to date)" }
}

if ($fail) {
    "`n=== COMPILE FAILURES ($($fail.Count)) ==="
    $fail | ForEach-Object { "`n--- $($_.Mod) ---`n$($_.Log)" }
    throw "Build aborted: $($fail.Count) module(s) failed to compile."
}
if ($CompileOnly) { "`nAll modules compiled OK in {0:N1}s." -f $sw.Elapsed.TotalSeconds; return }

# --- link: 128K tape + a 128K sna for quick emulator testing ---
$objs = ($csrcs + $asrcs) | ForEach-Object { "obj-zx128/$_.o" }
"`nlinking nexthack128.tap ..."
& zcc +zx -vn -clib=sdcc_iy -startup=1 -pragma-include:zpragma-zx128.inc -m $objs -o nexthack128 -create-app
"linking nexthack128.sna (128K) ..."
& zcc +zx -subtype=sna -vn -clib=sdcc_iy -startup=1 -pragma-include:zpragma-zx128.inc -m $objs -o nexthack128 -create-app -Cz"--128"
# z88dk's newlib +zx tape loader only loads the resident CODE, not the extra
# banks -> rebuild the .tap with a 128K loader that pages + loads each bank.
"rebuilding nexthack128.tap with the bank-paging 128K loader ..."
& python tools/mktap128.py
$sw.Stop()

if (Test-Path nexthack128.tap) {
    $code = (Select-String -Path nexthack128.map -Pattern '__CODE_END_tail\s+=\s+\$([0-9A-Fa-f]+)').Matches[0].Groups[1].Value
    $bss  = (Select-String -Path nexthack128.map -Pattern '__BSS_END_tail\s+=\s+\$([0-9A-Fa-f]+)').Matches[0].Groups[1].Value
    "OK: nexthack128.tap + .sna built in {0:N1}s.  resident __CODE_END=`${1}  __BSS_END=`${2}" -f $sw.Elapsed.TotalSeconds, $code, $bss
} else {
    throw "LINK FAILED."
}
