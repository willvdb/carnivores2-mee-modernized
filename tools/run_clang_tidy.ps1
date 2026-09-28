param(
    [Parameter(Mandatory = $true)]
    [string]$BuildDir,

    [string[]]$Files,

    [switch]$AllProjectFiles,

    [string]$RepoRoot = ''
)

$ErrorActionPreference = 'Stop'

if ([string]::IsNullOrWhiteSpace($RepoRoot)) {
    $RepoRoot = Split-Path $PSScriptRoot -Parent
}
$repoPath = (Resolve-Path -LiteralPath $RepoRoot -ErrorAction Stop).Path.TrimEnd([char[]]@('\', '/'))
$buildPath = (Resolve-Path -LiteralPath $BuildDir -ErrorAction Stop).Path
$databasePath = Join-Path $buildPath 'compile_commands.json'
$configPath = Join-Path $repoPath '.clang-tidy'

if (-not (Test-Path -LiteralPath $databasePath -PathType Leaf)) {
    throw "Compilation database not found: $databasePath. Configure a platform preset with -DCMAKE_EXPORT_COMPILE_COMMANDS=ON first."
}
if (-not (Test-Path -LiteralPath $configPath -PathType Leaf)) {
    throw "clang-tidy configuration not found: $configPath"
}

$clangTidy = Get-Command clang-tidy -ErrorAction SilentlyContinue
if (-not $clangTidy) {
    throw 'clang-tidy was not found on PATH.'
}

function Get-RepoRelativePath {
    param([string]$Path)

    $pathToResolve = $Path
    if (-not [System.IO.Path]::IsPathRooted($pathToResolve)) {
        $pathToResolve = Join-Path $repoPath $pathToResolve
    }
    if (-not (Test-Path -LiteralPath $pathToResolve -PathType Leaf)) {
        return $null
    }
    $fullPath = (Resolve-Path -LiteralPath $pathToResolve -ErrorAction Stop).Path
    $prefix = $repoPath + [System.IO.Path]::DirectorySeparatorChar
    if (-not $fullPath.StartsWith($prefix, [System.StringComparison]::OrdinalIgnoreCase)) {
        return $null
    }

    return $fullPath.Substring($prefix.Length).Replace('\', '/')
}

function Test-ProjectSource {
    param([string]$RelativePath)

    if ([string]::IsNullOrWhiteSpace($RelativePath)) {
        return $false
    }

    $normalized = $RelativePath.ToLowerInvariant()
    if ($normalized -like 'deps/*' -or
        $normalized -like 'build/*' -or
        $normalized -like 'install/*' -or
        $normalized -like '_deps/*') {
        return $false
    }

    return $normalized -match '\.(c|cc|cpp|cxx)$'
}

$compileEntries = @(Get-Content -LiteralPath $databasePath -Raw | ConvertFrom-Json)
$compileByPath = @{}
foreach ($entry in $compileEntries) {
    $relativePath = Get-RepoRelativePath -Path $entry.file
    if (Test-ProjectSource -RelativePath $relativePath) {
        $fullPath = (Resolve-Path -LiteralPath $entry.file -ErrorAction Stop).Path
        $compileByPath[$fullPath] = $entry.file
    }
}

$requestedFiles = @()
if ($AllProjectFiles) {
    $requestedFiles = @($compileByPath.Values)
}
elseif ($Files.Count -gt 0) {
    foreach ($file in $Files) {
        $relativePath = Get-RepoRelativePath -Path $file
        if (Test-ProjectSource -RelativePath $relativePath) {
            $fullPath = (Resolve-Path -LiteralPath (Join-Path $repoPath $relativePath) -ErrorAction Stop).Path
            if ($compileByPath.ContainsKey($fullPath)) {
                $requestedFiles += $compileByPath[$fullPath]
            }
        }
    }
}
else {
    throw 'Pass one or more changed source files with -Files, or explicitly use -AllProjectFiles.'
}

$requestedFiles = @($requestedFiles | Sort-Object -Unique)
if ($requestedFiles.Count -eq 0) {
    Write-Host 'No project-owned C/C++ translation units selected.'
    exit 0
}

$headerFilter = '[\\/]((Hunt|Menu|tests)[\\/]).*'
# cl.exe database entries do not always convey the target ABI to clang.
# Respect the selected fork architecture; Linux uses its database unchanged.
$targetArgs = @()
$cache = Get-Content -LiteralPath (Join-Path $buildPath 'CMakeCache.txt') -Raw
if ($cache -match '(?m)^CARNIVORES_ARCH:STRING=x86?$') { $targetArgs += '--extra-arg=-m32' }
elseif ($cache -match '(?m)^CARNIVORES_ARCH:STRING=x64?$') { $targetArgs += '--extra-arg=-m64' }
$failures = 0
foreach ($file in $requestedFiles) {
    & $clangTidy.Source $file `
        '-p' $buildPath `
        "--config-file=$configPath" `
        "--header-filter=$headerFilter" `
        @targetArgs
    if ($LASTEXITCODE -ne 0) {
        $failures++
    }
}

if ($failures -gt 0) {
    throw "clang-tidy failed for $failures translation unit(s)."
}
