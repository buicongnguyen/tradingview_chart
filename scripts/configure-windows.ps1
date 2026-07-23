[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Debug',
    [string]$QtRoot = '',
    [string]$BuildDirectory = ''
)

$ErrorActionPreference = 'Stop'

$repositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
if ([string]::IsNullOrWhiteSpace($QtRoot)) {
    $QtRoot = Join-Path $repositoryRoot '.qt\6.8.3\msvc2022_64'
}
if ([string]::IsNullOrWhiteSpace($BuildDirectory)) {
    $BuildDirectory = if ($Configuration -eq 'Release') {
        Join-Path $repositoryRoot 'build-release'
    } else {
        Join-Path $repositoryRoot 'build'
    }
}

$visualStudioRoot = 'C:\Program Files\Microsoft Visual Studio\2022\Community'
$cmake = Join-Path $visualStudioRoot 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'

if (-not (Test-Path -LiteralPath $cmake)) {
    $cmake = (Get-Command cmake -ErrorAction Stop).Source
}
if (-not (Test-Path -LiteralPath (Join-Path $QtRoot 'lib\cmake\Qt6WebEngineWidgets'))) {
    throw "Qt WebEngine was not found under $QtRoot. Run scripts\bootstrap-qt.ps1 first."
}

& $cmake `
    -S $repositoryRoot `
    -B $BuildDirectory `
    -G 'Visual Studio 17 2022' `
    -A x64 `
    "-DCMAKE_PREFIX_PATH=$QtRoot"
if ($LASTEXITCODE -ne 0) {
    throw "CMake configure failed with exit code $LASTEXITCODE."
}

Write-Host "Configured $Configuration build at $BuildDirectory"
