param(
    [ValidateSet("all", "iso", "run", "run-headless", "test-boot", "test-existing-iso", "debug-boot", "debug-run", "prepare-test-env", "clean")]
    [string]$Task = "iso",
    [string]$BashPath = $env:MSYS2_BASH
)

if (-not $BashPath -or -not (Test-Path $BashPath)) {
    $bashCommand = Get-Command bash.exe -ErrorAction SilentlyContinue
    if ($bashCommand) {
        $BashPath = $bashCommand.Source
    }
}

if (-not $BashPath -or -not (Test-Path $BashPath)) {
    throw "MSYS2 bash was not found. Install MSYS2 or set the MSYS2_BASH environment variable."
}

$projectRoot = Split-Path -Parent $PSScriptRoot
Push-Location $projectRoot

try {
    & $BashPath -lc "make $Task"
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
}
finally {
    Pop-Location
}
