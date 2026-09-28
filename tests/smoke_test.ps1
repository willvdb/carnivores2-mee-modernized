param(
    [string]$ExePath = "",
    [string]$LogFile = "",
    [ValidateRange(1, 2147483)]
    [int]$Duration = 5,
    [switch]$AllowMissingLog,
    [string]$WorkingDir = "",
    [string]$Preset = "hunt",
    [string]$Renderer = "GL",
    [string]$GameDir = "",
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$GameArgs = @(
        "reg=0",
        "prj=huntdat\areas\area1",
        "area=1",
        "din=1",
        "wep=1",
        "dtm=1"
    )
)

$ErrorActionPreference = 'Stop'

if (-not $Renderer) {
    $Renderer = "GL"
}
$Renderer = $Renderer.ToUpperInvariant()
if ($Renderer -notin @("GL", "SOFT")) {
    throw "Unsupported renderer '$Renderer'. Expected GL or SOFT."
}

# Named launch shapes for the deployed-asset smoke tier. Explicit -GameArgs (or
# trailing arguments) always win; a preset only replaces the defaults.
if (-not $PSBoundParameters.ContainsKey('GameArgs')) {
    switch ($Preset.ToLowerInvariant()) {
        'hunt' { }
        'trophy' {
            # The trophy room is an observer-style loadout: no creatures and no
            # weapons, which is what the standalone menu now emits.
            $GameArgs = @("reg=0", "prj=huntdat/areas/trophy", "din=0", "wep=0", "dtm=1")
        }
        default {
            throw "Unsupported preset '$Preset'. Expected hunt or trophy."
        }
    }
}

# By default smoke-test the deployed game, not the build artifact. The
# deployment task uses CARNIVORES_LEGACY_DIR and defaults to this location.
if (-not $GameDir) {
    $GameDir = $env:CARNIVORES_LEGACY_DIR
}
if (-not $GameDir) {
    $GameDir = "E:\Games\CarnivoresLegacy"
}
if (-not $ExePath) {
    $ExeName = if ($Renderer -eq "GL") { "v_gl.exe" } else { "v_soft.ren" }
    $ExePath = Join-Path $GameDir $ExeName
}
if (-not $WorkingDir) {
    $WorkingDir = Split-Path -Parent $ExePath
}
if (-not $LogFile) {
    $LogFile = Join-Path $WorkingDir "carnivor.log"
}

if (-not (Test-Path -LiteralPath $ExePath -PathType Leaf)) {
    throw "Deployed game executable not found: $ExePath. Run the VS Code copy task first."
}
if (-not (Test-Path -LiteralPath $WorkingDir -PathType Container)) {
    throw "Game working directory not found: $WorkingDir. Run the deployment task first."
}

# Resolve ExePath
$ExePath = (Resolve-Path -LiteralPath $ExePath -ErrorAction Stop).Path

Write-Host "=== Smoke Test ==="
Write-Host "  Exe:      $ExePath"
Write-Host "  Duration: $($Duration)s"
Write-Host "  Log:      $LogFile"
Write-Host "  WorkDir:  $WorkingDir"

# A game instance that is already running holds carnivor.log open and keeps
# writing to it. This test would then delete the file out from under it, read
# back the other session's log, and report whatever it finds there as a
# failure. Say so plainly instead of failing mysteriously later.
$aliases = @(
    [System.IO.Path]::GetFileNameWithoutExtension($ExePath),
    'v_gl', 'v_soft', 'v_d3d', 'v_3dfx', 'Carnivores1_GL', 'Carnivores1_SOFT'
) | Select-Object -Unique
$running = Get-Process -Name $aliases -ErrorAction SilentlyContinue
if ($running) {
    throw "A game instance is already running (PID $(($running.Id) -join ', ')). Close it and re-run - this test needs carnivor.log to itself."
}

# Clean previous log
if (Test-Path $LogFile) {
    try {
        Remove-Item $LogFile -Force -ErrorAction Stop
    } catch {
        throw "Could not delete $LogFile - $($_.Exception.Message)"
    }
    Write-Host "  Removed old log file"
}

Write-Host "Launching deployed game..."
Write-Host "  Args:     $($GameArgs -join ' ')"
$proc = Start-Process -FilePath $ExePath -ArgumentList $GameArgs -WorkingDirectory $WorkingDir -PassThru -NoNewWindow
Write-Host "  PID: $($proc.Id)"
Write-Host "  Waiting $($Duration) seconds..."

$sw = [System.Diagnostics.Stopwatch]::StartNew()

try {
    $timedOut = $false
    while ($sw.Elapsed.TotalSeconds -lt $Duration) {
        Start-Sleep -Milliseconds 200
        if ($proc.HasExited) {
            break
        }
    }

    if (-not $proc.HasExited) {
        $timedOut = $true
        Write-Host "Time's up - killing process..."
        $proc.Kill()
        $null = $proc.WaitForExit(2000)
        Write-Host "Process terminated after the requested timeout"
    }

    if (-not $timedOut) {
        $elapsed = $sw.Elapsed.TotalSeconds.ToString('F1')
        $exitCode = $proc.ExitCode
        throw "Game exited early after ${elapsed}s (exit code: $exitCode)"
    }
}
finally {
    if ($proc -and -not $proc.HasExited) {
        try {
            $proc.Kill()
            $null = $proc.WaitForExit(2000)
        }
        catch [System.InvalidOperationException] {
            # The process can exit between HasExited and Kill.
        }
    }
}

# Check log for errors
if (Test-Path -LiteralPath $LogFile -PathType Leaf) {
    $logContent = Get-Content -LiteralPath $LogFile -Raw
    if ([string]::IsNullOrWhiteSpace($logContent)) {
        throw "Log file is empty: $LogFile"
    }
    $errorPattern = 'ABNORMAL_HALT|ERROR|FATAL|assertion'
    if ($logContent -match $errorPattern) {
        Write-Host "Found error markers in log:"
        $logContent | Select-String -Pattern $errorPattern | ForEach-Object { Write-Host "  $_" }
        throw "Smoke log contains error markers: $LogFile"
    }
    $logSize = (Get-Item $LogFile).Length
    Write-Host "  Log: $logSize bytes (no errors)"
} else {
    if ($AllowMissingLog) {
        Write-Warning "No log file found at $LogFile (allowed by -AllowMissingLog)"
    } else {
        throw "Required log file was not created: $LogFile"
    }
}

Write-Host "=== Smoke Test PASSED ==="
