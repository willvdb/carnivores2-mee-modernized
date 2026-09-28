param(
    [Parameter(Mandatory = $true)]
    [string]$BuildDir
)

$ErrorActionPreference = 'Stop'

$buildPath = (Resolve-Path -LiteralPath $BuildDir -ErrorAction Stop).Path
$ninjaPath = Join-Path $buildPath 'build.ninja'
if (-not (Test-Path -LiteralPath $ninjaPath -PathType Leaf)) {
    throw "Ninja build file not found: $ninjaPath"
}

$flagLines = @(Get-Content -LiteralPath $ninjaPath |
    Where-Object { $_ -match '^\s*FLAGS\s*=' })
if ($flagLines.Count -eq 0) {
    throw "No compiler flag lines found in $ninjaPath"
}

$flags = $flagLines -join "`n"
if ($flags -match '(?i)(^|\s)/GS-') {
    throw "Release compiler flags disable stack protection (/GS-): $ninjaPath"
}
if ($flags -notmatch '(?i)(^|\s)/GS(?=\s|$)') {
    throw "Release compiler flags do not enable stack protection (/GS): $ninjaPath"
}

Write-Host "Release compiler flags enable /GS and contain no /GS-: $ninjaPath"
