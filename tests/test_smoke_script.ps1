param(
    [Parameter(Mandatory = $true)]
    [string]$FixtureDll
)

$ErrorActionPreference = 'Stop'

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$smokeScript = Join-Path $PSScriptRoot 'smoke_test.ps1'
$dotnet = (Get-Command dotnet -ErrorAction Stop).Source

function Invoke-SmokeCase {
    param(
        [string]$Name,
        [string]$Mode,
        [string]$ExitMode,
        [bool]$ExpectedSuccess,
        [bool]$AllowMissingLog,
        [string]$Root
    )

    $logPath = Join-Path $Root "$Name.log"
    $parameters = @{
        ExePath = $dotnet
        GameDir = $Root
        WorkingDir = $Root
        LogFile = $logPath
        Duration = 1
        GameArgs = @($FixtureDll, $Mode, $ExitMode, $logPath)
    }
    if ($AllowMissingLog) {
        $parameters.AllowMissingLog = $true
    }

    $failed = $false
    try {
        & $smokeScript @parameters
    }
    catch {
        $failed = $true
        if ($ExpectedSuccess) {
            throw
        }
    }

    if ($failed -eq $ExpectedSuccess) {
        throw "Smoke contract '$Name' expected success=$ExpectedSuccess but observed failure=$failed."
    }

    Write-Host "Smoke contract passed: $Name"
}

$root = Join-Path ([System.IO.Path]::GetTempPath()) "c2-smoke-contract-$PID"
New-Item -ItemType Directory -Path $root -Force | Out-Null

try {
    Invoke-SmokeCase -Name 'clean-timeout' -Mode 'clean' -ExitMode 'wait' `
        -ExpectedSuccess $true -AllowMissingLog $false -Root $root
    Invoke-SmokeCase -Name 'early-exit' -Mode 'clean' -ExitMode '0' `
        -ExpectedSuccess $false -AllowMissingLog $false -Root $root
    Invoke-SmokeCase -Name 'error-log' -Mode 'error' -ExitMode 'wait' `
        -ExpectedSuccess $false -AllowMissingLog $false -Root $root
    Invoke-SmokeCase -Name 'empty-log' -Mode 'empty' -ExitMode 'wait' `
        -ExpectedSuccess $false -AllowMissingLog $false -Root $root
    Invoke-SmokeCase -Name 'whitespace-log' -Mode 'whitespace' -ExitMode 'wait' `
        -ExpectedSuccess $false -AllowMissingLog $false -Root $root
    Invoke-SmokeCase -Name 'missing-log' -Mode 'missing' -ExitMode 'wait' `
        -ExpectedSuccess $false -AllowMissingLog $false -Root $root
    Invoke-SmokeCase -Name 'missing-log-allowed' -Mode 'missing' -ExitMode 'wait' `
        -ExpectedSuccess $true -AllowMissingLog $true -Root $root
}
finally {
    if (Test-Path -LiteralPath $root) {
        Remove-Item -LiteralPath $root -Recurse -Force -ErrorAction SilentlyContinue
    }
}
