# Validate banks.json against what the builds actually compile.
#
# It calls the SAME Import-BankManifest the build uses rather than
# re-implementing the rules: one copy of "every module must be declared" and
# "colocate groups must share a bank", so the check cannot drift from the
# behaviour it is checking. All this adds is the module lists, read out of the
# build scripts, and the existence of the sources.
#
#   pwsh tools/check-manifest.ps1
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
. (Join-Path $root 'banks.ps1')

function Get-ModuleList([string]$file, [string]$var) {
    $pattern = '^\s*\$' + $var + '\s*='
    $line = Get-Content (Join-Path $root $file) | Where-Object { $_ -match $pattern }
    if (-not $line) { throw "$file : no `$$var assignment found" }
    $names = [regex]::Matches($line, "'([^']+)'") | ForEach-Object { $_.Groups[1].Value }
    if (-not $names) { throw "$file : `$$var is empty" }
    return $names
}

$next  = Get-ModuleList 'build.ps1'       'srcs'
$zx128 = Get-ModuleList 'build-zx128.ps1' 'csrcs'
"  build.ps1 compiles $($next.Count) modules, build-zx128.ps1 $($zx128.Count)"

# throws on an undeclared module, a stale declaration, or a split colocate group
Import-BankManifest -Target next  -Modules $next  | Out-Null
Import-BankManifest -Target zx128 -Modules $zx128 | Out-Null
"  banks.json: every module declared, no stale entries, colocate groups intact"

$missing = @()
foreach ($m in (($next + $zx128) | Sort-Object -Unique)) {
    if (-not (Test-Path (Join-Path $root "src/$m.c"))) { $missing += $m }
}
if ($missing) { throw "declared but no source file: $($missing -join ', ')" }
"  all $((($next + $zx128) | Sort-Object -Unique).Count) modules have a src/*.c"

# a bank a module is assigned to must be either in the pool or deliberately
# pinned outside it (the Layer 2 image banks); a typo would land somewhere the
# packer never looks at and the linker never ORGs
$man = Get-Content (Join-Path $root 'banks.json') -Raw | ConvertFrom-Json
foreach ($target in 'next', 'zx128') {
    $pool = $man.$target._pool
    foreach ($p in $man.$target.PSObject.Properties) {
        if ($p.Name.StartsWith('_')) { continue }
        foreach ($b in @($p.Value.code, $p.Value.const)) {
            if ($b -and $b -notin $pool -and $b -notmatch '^BANK_(1[6-9]|2[01])$') {
                throw "$target/$($p.Name): bank '$b' is neither in _pool nor a Layer 2 image bank"
            }
        }
    }
}
"  every assignment names a real bank"
