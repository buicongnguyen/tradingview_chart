[CmdletBinding()]
param(
    [string]$QtVersion = '6.8.3',
    [ValidateSet('android_arm64_v8a', 'android_x86_64')]
    [string]$QtArchitecture = 'android_arm64_v8a',
    [string]$AndroidSdkRoot =
        (Join-Path $env:LOCALAPPDATA 'Android\Sdk'),
    [string]$NdkVersion = '26.1.10909125',
    [string]$PythonCommand = 'py'
)

$ErrorActionPreference = 'Stop'

$repositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$qtOutput = Join-Path $repositoryRoot '.qt'
$qtRoot = Join-Path $qtOutput "$QtVersion\$QtArchitecture"
$requiredModule = Join-Path $qtRoot 'lib\cmake\Qt6WebView'
$ndkRoot = Join-Path $AndroidSdkRoot "ndk\$NdkVersion"
$requiredSdkPaths = @(
    (Join-Path $ndkRoot 'source.properties'),
    (Join-Path $AndroidSdkRoot 'platform-tools\adb.exe'),
    (Join-Path $AndroidSdkRoot 'platforms\android-36\android.jar'),
    (Join-Path $AndroidSdkRoot 'build-tools\36.0.0\aapt.exe')
)

$sdkManager = Join-Path $AndroidSdkRoot 'cmdline-tools\latest\bin\sdkmanager.bat'
if (-not (Test-Path -LiteralPath $sdkManager)) {
    throw "Android sdkmanager was not found at $sdkManager."
}
if ($requiredSdkPaths.Where({
            -not (Test-Path -LiteralPath $_)
        }).Count -gt 0) {
    & $sdkManager "ndk;$NdkVersion" 'platform-tools' `
        'platforms;android-36' 'build-tools;36.0.0'
    if ($LASTEXITCODE -ne 0) {
        throw "Android SDK/NDK installation failed with exit code $LASTEXITCODE."
    }
}

if (-not (Test-Path -LiteralPath $requiredModule)) {
    $venvRoot = Join-Path $repositoryRoot '.tools\aqt'
    $venvPython = Join-Path $venvRoot 'Scripts\python.exe'
    if (-not (Test-Path -LiteralPath $venvPython)) {
        & $PythonCommand -m venv $venvRoot
        if ($LASTEXITCODE -ne 0) {
            throw 'Could not create the aqtinstall Python environment.'
        }
    }
    & $venvPython -m pip install --disable-pip-version-check 'aqtinstall==3.3.0'
    if ($LASTEXITCODE -ne 0) {
        throw 'Could not install aqtinstall.'
    }
    & $venvPython -m aqt install-qt `
        windows android $QtVersion $QtArchitecture `
        --outputdir $qtOutput `
        --modules qtwebview
    if ($LASTEXITCODE -ne 0) {
        throw "Qt for Android installation failed with exit code $LASTEXITCODE."
    }
}

if (-not (Test-Path -LiteralPath $requiredModule)) {
    throw "Qt WebView was not installed under $qtRoot."
}

Write-Host "Android toolchain ready:"
Write-Host "  Qt:  $qtRoot"
Write-Host "  SDK: $AndroidSdkRoot"
Write-Host "  NDK: $ndkRoot"
