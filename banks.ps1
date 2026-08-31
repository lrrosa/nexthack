# === NextHack bank manifest reader (shared by build.ps1 and build-zx128.ps1) ===
#
# banks.json says which bank each module's code and consts belong to. This turns
# that into zcc flags, and validates the two invariants a wrong bank produces:
#
#   1. every module the build compiles is declared (and nothing stale is left
#      declared) -- an undeclared module would silently land in the RESIDENT
#      half and blow the stack floor;
#   2. the 'colocate' groups all resolve to ONE bank -- a module that hands a
#      const or a string literal to code in another bank compiles, links, passes
#      the 16 KB guards and then reads garbage at runtime with that bank paged
#      out. That is the one bank mistake no size check can catch, so it is
#      checked here, before anything is compiled.
#
# Import-BankManifest returns a hashtable: module -> @{ Flags; Stamp }.
# Flags feeds zcc; Stamp is written next to the .o so that changing a module's
# bank recompiles exactly that module (see build.ps1's staleness rule).

function Import-BankManifest {
    param(
        [Parameter(Mandatory)][ValidateSet('next', 'zx128')][string]$Target,
        [Parameter(Mandatory)][string[]]$Modules,
        [string]$Path = 'banks.json'
    )

    if (-not (Test-Path $Path)) { throw "Bank manifest '$Path' not found -- it declares which bank every module lives in." }
    try { $man = Get-Content $Path -Raw | ConvertFrom-Json }
    catch { throw "Bank manifest '$Path' is not valid JSON: $($_.Exception.Message)" }

    $tm = $man.$Target
    if (-not $tm) { throw "Bank manifest '$Path' has no '$Target' section." }

    # --- invariant 1: the declared module set matches what we are building ---
    # Keys starting with '_' are metadata (_doc, _pool, ...), not modules.
    $declared = $tm.PSObject.Properties.Name | Where-Object { -not $_.StartsWith('_') }
    $missing  = $Modules  | Where-Object { $_ -notin $declared }
    $extra    = $declared | Where-Object { $_ -notin $Modules }
    if ($missing) {
        throw ("Bank manifest '$Path' ($Target) does not declare: $($missing -join ', ').`n" +
               "       Add an entry per module -- no keys means RESIDENT, which costs stack-floor headroom.")
    }
    if ($extra) {
        throw "Bank manifest '$Path' ($Target) declares modules this target does not build: $($extra -join ', ')."
    }

    # --- build the flag table ---
    $out = @{}
    foreach ($m in $Modules) {
        $e = $tm.$m
        $flags = @()
        if ($e.code)  { $flags += @('--codeseg',  $e.code) }
        if ($e.const) { $flags += @('--constseg', $e.const) }
        $out[$m] = @{
            Flags = $flags
            Stamp = "$Target code=$($e.code) const=$($e.const)"
            Banks = @(@($e.code, $e.const | Where-Object { $_ }) | Sort-Object -Unique)
        }
    }

    # --- invariant 2: colocate groups resolve to a single bank ---
    foreach ($g in $man.colocate) {
        $inTarget = $g.modules | Where-Object { $_ -in $Modules }
        if ($inTarget.Count -lt 2) { continue }   # group is for the other target
        $banks = $inTarget | ForEach-Object { $out[$_].Banks } | Sort-Object -Unique
        if ($banks.Count -gt 1) {
            $where = ($inTarget | ForEach-Object { "$_ -> $($out[$_].Banks -join '+')" }) -join ', '
            throw ("Bank manifest '$Path' ($Target): modules that must share a bank are split across $($banks -join ' and ').`n" +
                   "       $where`n" +
                   "       Reason: $($g.why)")
        }
    }

    return $out
}
