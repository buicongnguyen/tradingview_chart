[CmdletBinding()]
param(
    [string]$QtVersion = '6.8.3',
    [string]$QtArchitecture = 'win64_msvc2022_64',
    [string]$PythonCommand = 'py'
)

$ErrorActionPreference = 'Stop'

$repositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$qtOutput = Join-Path $repositoryRoot '.qt'
$qtRoot = Join-Path $qtOutput "$QtVersion\msvc2022_64"
$requiredModules = @(
    (Join-Path $qtRoot 'lib\cmake\Qt6WebEngineWidgets'),
    (Join-Path $qtRoot 'lib\cmake\Qt6WebChannel')
)

if (($requiredModules | Where-Object { -not (Test-Path -LiteralPath $_) }).Count -eq 0) {
    Write-Host "Qt $QtVersion with WebEngine is already installed at $qtRoot"
    exit 0
}

$venvRoot = Join-Path $repositoryRoot '.tools\aqt'
$venvPython = Join-Path $venvRoot 'Scripts\python.exe'
if (-not (Test-Path -LiteralPath $venvPython)) {
    & $PythonCommand -m venv $venvRoot
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $venvPython)) {
        throw "Could not create the aqtinstall Python environment with '$PythonCommand'."
    }
}

& $venvPython -m pip install --disable-pip-version-check 'aqtinstall==3.3.0'
& $venvPython -m aqt install-qt `
    windows desktop $QtVersion $QtArchitecture `
    --outputdir $qtOutput `
    --modules qtwebengine qtwebchannel qtpositioning

foreach ($modulePath in $requiredModules) {
    if (-not (Test-Path -LiteralPath $modulePath)) {
        throw "Qt bootstrap completed without required module: $modulePath"
    }
}

Write-Host "Qt $QtVersion with WebEngine installed at $qtRoot"
