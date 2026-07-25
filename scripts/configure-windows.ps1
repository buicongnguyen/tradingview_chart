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

$cmakeCommand = Get-Command cmake -ErrorAction SilentlyContinue
$cmake = if ($null -ne $cmakeCommand) { $cmakeCommand.Source } else { '' }
if ([string]::IsNullOrWhiteSpace($cmake)) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path -LiteralPath $vswhere) {
        $visualStudioRoot = (& $vswhere `
            -latest `
            -products '*' `
            -version '[17.0,18.0)' `
            -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
            -property installationPath | Select-Object -First 1)
        if (-not [string]::IsNullOrWhiteSpace($visualStudioRoot)) {
            $candidate = Join-Path $visualStudioRoot 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
            if (Test-Path -LiteralPath $candidate) {
                $cmake = $candidate
            }
        }
    }
}
if ([string]::IsNullOrWhiteSpace($cmake) -or -not (Test-Path -LiteralPath $cmake)) {
    throw 'CMake was not found in PATH or a Visual Studio 2022 C++ installation.'
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
