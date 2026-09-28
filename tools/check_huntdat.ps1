<#
.SYNOPSIS
    Checks a Carnivores 2 MEE game folder for the data problems that halt the
    engine at map load.

.DESCRIPTION
    The engine opens data by paths recorded in its own scripts, and a missing
    file is a fatal halt rather than a skip. This walks the same paths so a
    support ticket can be answered from the output instead of guesswork:

      - every model path in HUNTDAT/_RES.TXT that does not exist, with the entry
        that names it
      - every area slot listed in HUNTDAT/_MENU.TXT (or its _RES.TXT fallback)
        whose .map/.rsc pair is incomplete (the game would then halt)
      - .c2map descriptors whose launch path has no complete pair
      - the trophy room pair the menu opens (huntdat/areas/trophy.map/.rsc)
      - the description and thumbnail files each slot reads

    Roots match the engine: a character 'file' resolves under HUNTDAT, a weapon
    'file'/bModel under HUNTDAT\WEAPONS. Textures and sounds are not checked --
    the engine reports or tolerates those differently, so listing them here
    would only add noise.

    Nothing is modified; the report can be pasted into a ticket. Errors exit 1;
    -Strict also fails on warnings.

.PARAMETER GameDir
    The game folder, i.e. the one containing HUNTDAT. Defaults to the current
    directory.

.PARAMETER Strict
    Treat warnings as failures as well.

.EXAMPLE
    .\check_huntdat.ps1 'C:\Games\Carnivores 2'

.EXAMPLE
    .\check_huntdat.ps1 -Strict -GameDir 'E:\Games\CarnivoresLegacy' > huntdat-check.txt
#>
param(
    [string]$GameDir = '.',
    [switch]$Strict
)

$ErrorActionPreference = 'Stop'

$errors   = New-Object System.Collections.Generic.List[string]
$warnings = New-Object System.Collections.Generic.List[string]

function Test-GameFile {
    param([string]$Path)
    # Literal, wildcard-free check: data-supplied names may contain [ ] which
    # Test-Path would treat as a pattern.
    return [System.IO.File]::Exists($Path)
}

function Get-RelativePath {
    param([string]$Value)
    # Data paths appear with either separator, and INI-style samples double the
    # backslashes ('huntdat\\areas\\area1').
    $p = $Value.Replace('\\', '/').Replace('\', '/')
    while ($p.Contains('//')) { $p = $p.Replace('//', '/') }
    return $p
}

if (-not (Test-Path -LiteralPath $GameDir -PathType Container)) {
    Write-Output "Game folder not found: $GameDir"
    exit 1
}
$GameDir = (Resolve-Path -LiteralPath $GameDir).Path
$huntdat = Join-Path $GameDir 'HUNTDAT'

if (-not (Test-Path -LiteralPath $huntdat -PathType Container)) {
    Write-Output "No HUNTDAT folder in: $GameDir"
    Write-Output "Run this from the game folder (the one holding the .ren/.exe files)."
    exit 1
}

# --- HUNTDAT/_RES.TXT --------------------------------------------------------
# One 'file = <path>' per model. The engine prefixes HUNTDAT for characters and
# HUNTDAT\WEAPONS for weapon entries, then halts if the result cannot be opened,
# so a bad path here breaks every map that loads that species or weapon. Track
# the nearest 'name = ...' for the report and the section for the root.
$resTxt = Join-Path $huntdat '_RES.TXT'
$resChecked = 0
if (Test-GameFile $resTxt) {
    $inWeapons = $false
    $entryName = ''
    $lineNo = 0
    foreach ($line in [System.IO.File]::ReadAllLines($resTxt)) {
        $lineNo++

        # Any named block at the start of a line ends the previous section; the
        # weapons block is the only one whose paths root at HUNTDAT\WEAPONS.
        if ($line -match '^\s*([A-Za-z_][A-Za-z0-9_]*)\s*\{') {
            $inWeapons = ($Matches[1] -eq 'weapons')
        }
        if ($line -match "^\s*name\s*=\s*'([^']*)'") {
            $entryName = $Matches[1]
            continue
        }

        $key = ''
        if ($line -match "^\s*file\s*=\s*'([^']*)'") { $key = 'file' }
        elseif ($inWeapons -and ($line -match "^\s*bModel\s*=\s*'([^']*)'")) { $key = 'bModel' }

        if ($key -ne '') {
            $value = $Matches[1]
            # Bare filenames are still opened relative to the same root. Do not
            # guess an extension or search for an alternative: test the exact
            # name passed to the engine, including extensionless names.
            $resChecked++
            $root = $huntdat
            if ($inWeapons) { $root = Join-Path $huntdat 'WEAPONS' }
            $full = (Join-Path $root (Get-RelativePath $value)).Replace('/', '\')
            if (-not (Test-GameFile $full)) {
                $errors.Add("missing model  $full   <- _RES.TXT:$lineNo entry '$entryName'")
            }
        }
    }
} else {
    $errors.Add("missing file   $resTxt   (the resource script the engine reads)")
}

# --- HUNTDAT/AREAS -----------------------------------------------------------
# The engine opens both <basename>.map and <basename>.rsc from the single
# project name it is launched with, so half a pair is not a usable area.
$areasDir = Join-Path $huntdat 'areas'
$areaMaps = @{}
$areaRscs = @{}
if (Test-Path -LiteralPath $areasDir -PathType Container) {
    foreach ($file in [System.IO.Directory]::GetFiles($areasDir)) {
        $extension = [System.IO.Path]::GetExtension($file).ToLowerInvariant()
        $stem = [System.IO.Path]::GetFileNameWithoutExtension($file).ToLowerInvariant()
        if ($extension -eq '.map') { $areaMaps[$stem] = [System.IO.Path]::GetFileName($file) }
        elseif ($extension -eq '.rsc') { $areaRscs[$stem] = [System.IO.Path]::GetFileName($file) }
    }
} else {
    $errors.Add("missing dir    $areasDir   (no area files at all)")
}

# --- HUNTDAT menu price list -------------------------------------------------
# The menu uses _RES.TXT if _MENU.TXT cannot be opened (Menu/Resources.cpp).
# Read the same source to count its 'area = <price>' entries.
$menuTxt = Join-Path $huntdat '_MENU.TXT'
$slotScript = $menuTxt
$slotSource = '_MENU.TXT'
$slotCount = 0
if (-not (Test-GameFile $menuTxt)) {
    $warnings.Add("missing file   $menuTxt   (menu falls back to _RES.TXT)")
    $slotScript = $resTxt
    $slotSource = '_RES.TXT'
}
if (Test-GameFile $slotScript) {
    foreach ($line in [System.IO.File]::ReadAllLines($slotScript)) {
        if ($line -match '^\s*area\s*=\s*') { $slotCount++ }
    }
}

for ($slot = 1; $slot -le $slotCount; $slot++) {
    # The vanilla sixth slot keeps its assets as external.map/.rsc, so either
    # basename is the slot; anything else is areaN.
    $candidates = @("area$slot")
    if ($slot -eq 6) { $candidates = @('external', 'area6') }

    $complete = $false
    $halves = @()
    foreach ($base in $candidates) {
        $key = $base.ToLowerInvariant()
        if ($areaMaps.ContainsKey($key) -and $areaRscs.ContainsKey($key)) {
            $complete = $true
        }
        elseif ($areaMaps.ContainsKey($key)) {
            $halves += "$($areaMaps[$key]) has no matching .rsc"
        }
        elseif ($areaRscs.ContainsKey($key)) {
            $halves += "$($areaRscs[$key]) has no matching .map"
        }
    }

    if (-not $complete) {
        if ($halves.Count -gt 0) {
            $errors.Add("incomplete area slot $slot is listed in $($slotSource): " + ($halves -join '; ') + " (its launch is refused)")
        } else {
            $errors.Add("missing area   slot $slot is listed in $slotSource but $(($candidates -join ' and ')) have no .map/.rsc pair")
        }
    }

    $text  = Join-Path $huntdat "menu\txt\area$slot.txt"
    $thumb = Join-Path $huntdat "menu\pics\area$slot.tga"
    if (-not (Test-GameFile $text)) { $warnings.Add("menu data      $text is missing (slot $slot has no description)") }
    if (-not (Test-GameFile $thumb)) { $warnings.Add("menu data      $thumb is missing (slot $slot has no thumbnail)") }
}

# Pairs no slot and no descriptor uses: not fatal, but usually leftovers. The
# room is opened by its own menu entry rather than a slot, so it counts as used.
$usedStems = New-Object System.Collections.Generic.HashSet[string]
for ($slot = 1; $slot -le $slotCount; $slot++) {
    if ($slot -eq 6) {
        [void]$usedStems.Add('external')
        [void]$usedStems.Add('area6')
    } else {
        [void]$usedStems.Add("area$slot")
    }
}
[void]$usedStems.Add('trophy')

foreach ($stem in ($areaMaps.Keys | Sort-Object)) {
    if (-not $usedStems.Contains($stem) -and -not $areaRscs.ContainsKey($stem)) {
        $warnings.Add("orphan file    $areasDir\$($areaMaps[$stem]) is not used by any slot and has no .rsc")
    }
}
foreach ($stem in ($areaRscs.Keys | Sort-Object)) {
    if (-not $usedStems.Contains($stem) -and -not $areaMaps.ContainsKey($stem)) {
        $warnings.Add("orphan file    $areasDir\$($areaRscs[$stem]) is not used by any slot and has no .map")
    }
}

# --- HUNTDAT/AREAS/*.c2map and the trophy room -------------------------------
if (Test-Path -LiteralPath $areasDir -PathType Container) {
    foreach ($descriptor in [System.IO.Directory]::GetFiles($areasDir, '*.c2map')) {
        $text = [System.IO.File]::ReadAllText($descriptor)
        if ($text -match "(?im)^\s*mapfile\s*=\s*'([^']*)'") {
            # The launch prefixes the project stem with huntdat/areas, so that
            # pair is what the engine will open.
            $stem = [System.IO.Path]::GetFileNameWithoutExtension((Get-RelativePath $Matches[1]))
            $missing = @()
            if (-not $areaMaps.ContainsKey($stem.ToLowerInvariant())) { $missing += ($stem + '.map') }
            if (-not $areaRscs.ContainsKey($stem.ToLowerInvariant())) { $missing += ($stem + '.rsc') }
            if ($missing.Count -gt 0) {
                $name = [System.IO.Path]::GetFileName($descriptor)
                $errors.Add("incomplete area $name launches as huntdat/areas/$stem but $(($missing -join ' and ')) is missing (its launch is refused)")
            }
        }
    }

    $trophyMissing = @()
    if (-not $areaMaps.ContainsKey('trophy')) { $trophyMissing += 'trophy.map' }
    if (-not $areaRscs.ContainsKey('trophy')) { $trophyMissing += 'trophy.rsc' }
    if ($trophyMissing.Count -gt 0) {
        $errors.Add("missing area   $areasDir\$(($trophyMissing -join ' and '))   (the room the menu opens)")
    }
}

# --- report ------------------------------------------------------------------
Write-Output "Carnivores 2 data check: $GameDir"
Write-Output "  _RES.TXT model paths checked: $resChecked"
Write-Output "  $slotSource area slots: $slotCount; area files: $($areaMaps.Count) .map / $($areaRscs.Count) .rsc"

if ($errors.Count -gt 0) {
    Write-Output ""
    Write-Output "ERRORS ($($errors.Count))"
    foreach ($entry in $errors) { Write-Output "  $entry" }
}
if ($warnings.Count -gt 0) {
    Write-Output ""
    Write-Output "WARNINGS ($($warnings.Count))"
    foreach ($entry in $warnings) { Write-Output "  $entry" }
}
if ($errors.Count -eq 0 -and $warnings.Count -eq 0) {
    Write-Output "  No problems found."
}

Write-Output ""
Write-Output "$($errors.Count) error(s), $($warnings.Count) warning(s)"

if ($errors.Count -gt 0 -or ($Strict -and $warnings.Count -gt 0)) { exit 1 }
exit 0
