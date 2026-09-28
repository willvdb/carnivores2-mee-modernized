$ErrorActionPreference = 'Stop'

$checker = Join-Path $PSScriptRoot '../tools/check_huntdat.ps1'
$root = Join-Path ([System.IO.Path]::GetTempPath()) ("c2-huntdat-check-" + [guid]::NewGuid().ToString('N'))
$huntdat = Join-Path $root 'HUNTDAT'
$areas = Join-Path $huntdat 'areas'

function Invoke-Check {
    param([int]$ExpectedExit, [string]$ExpectedText)

    $output = @(& $checker -GameDir $root)
    $code = $LASTEXITCODE
    $text = $output -join "`n"
    if ($code -ne $ExpectedExit -or -not $text.Contains($ExpectedText)) {
        throw "Expected exit $ExpectedExit and '$ExpectedText', got exit $($code):`n$text"
    }
}

try {
    New-Item -ItemType Directory -Path $areas -Force | Out-Null
    New-Item -ItemType Directory -Path (Join-Path $huntdat 'menu/txt') -Force | Out-Null
    New-Item -ItemType Directory -Path (Join-Path $huntdat 'menu/pics') -Force | Out-Null
    foreach ($file in @('trophy.map', 'trophy.rsc', 'area1.map', 'area1.rsc')) {
        [System.IO.File]::WriteAllText((Join-Path $areas $file), '')
    }
    [System.IO.File]::WriteAllText((Join-Path $huntdat 'menu/txt/area1.txt'), '')
    [System.IO.File]::WriteAllText((Join-Path $huntdat 'menu/pics/area1.tga'), '')
    [System.IO.File]::WriteAllText((Join-Path $huntdat '_MENU.TXT'), "prices {`n area = 0`n}`n")
    [System.IO.File]::WriteAllText((Join-Path $huntdat '_RES.TXT'), "characters {`n{`n name = 'Test'`n file = 'BareModel'`n}`n}`nprices {`n area = 0`n}`n")
    [System.IO.File]::WriteAllText((Join-Path $huntdat 'BareModel'), '')

    Invoke-Check -ExpectedExit 0 -ExpectedText '0 error(s), 0 warning(s)'
    Remove-Item -LiteralPath (Join-Path $huntdat 'BareModel')
    Invoke-Check -ExpectedExit 1 -ExpectedText 'missing model'
    [System.IO.File]::WriteAllText((Join-Path $huntdat 'BareModel'), '')

    # The menu's supported fallback reads prices from _RES.TXT when _MENU.TXT
    # is absent. A missing half must still be fatal to the validator.
    Remove-Item -LiteralPath (Join-Path $huntdat '_MENU.TXT')
    Remove-Item -LiteralPath (Join-Path $areas 'area1.rsc')
    Invoke-Check -ExpectedExit 1 -ExpectedText 'slot 1 is listed in _RES.TXT'
    [System.IO.File]::WriteAllText((Join-Path $areas 'area1.rsc'), '')
    Invoke-Check -ExpectedExit 0 -ExpectedText '_RES.TXT area slots: 1'

    Write-Output 'HUNTDAT validator contract tests passed.'
}
finally {
    if (Test-Path -LiteralPath $root) {
        Remove-Item -LiteralPath $root -Recurse -Force
    }
}
