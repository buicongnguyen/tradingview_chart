[CmdletBinding()]
param(
    [ValidateSet('Release')]
    [string]$Configuration = 'Release',
    [string]$QtRoot = ''
)

$ErrorActionPreference = 'Stop'

$repositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
if ([string]::IsNullOrWhiteSpace($QtRoot)) {
    $QtRoot = Join-Path $repositoryRoot '.qt\6.8.3\msvc2022_64'
}
$buildDirectory = Join-Path $repositoryRoot 'build-release'
$distRoot = Join-Path $repositoryRoot 'dist'
$stage = Join-Path $distRoot 'TradingViewChart-0.1.0-win64'
$archive = "$stage.zip"

& (Join-Path $PSScriptRoot 'configure-windows.ps1') `
    -Configuration $Configuration `
    -QtRoot $QtRoot `
    -BuildDirectory $buildDirectory

$visualStudioRoot = 'C:\Program Files\Microsoft Visual Studio\2022\Community'
$cmake = Join-Path $visualStudioRoot 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
if (-not (Test-Path -LiteralPath $cmake)) {
    $cmake = (Get-Command cmake -ErrorAction Stop).Source
}
$ctest = Join-Path (Split-Path $cmake -Parent) 'ctest.exe'

& $cmake --build $buildDirectory --config $Configuration
if ($LASTEXITCODE -ne 0) {
    throw "Release build failed with exit code $LASTEXITCODE."
}

$qtBin = Join-Path $QtRoot 'bin'
$originalPath = $env:PATH
try {
    $env:PATH = "$qtBin;$originalPath"
    & $ctest --test-dir $buildDirectory -C $Configuration --output-on-failure
    if ($LASTEXITCODE -ne 0) {
        throw "Release tests failed with exit code $LASTEXITCODE."
    }
} finally {
    $env:PATH = $originalPath
}

if (Test-Path -LiteralPath $distRoot) {
    $resolvedRepository = [System.IO.Path]::GetFullPath($repositoryRoot)
    $resolvedDist = [System.IO.Path]::GetFullPath($distRoot)
    if (-not $resolvedDist.StartsWith("$resolvedRepository\", [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to replace an output directory outside the repository."
    }
    Remove-Item -LiteralPath $distRoot -Recurse -Force
}
New-Item -ItemType Directory -Path $stage | Out-Null

Copy-Item -LiteralPath (Join-Path $buildDirectory "$Configuration\tradingview_chart.exe") -Destination $stage
Copy-Item -LiteralPath (Join-Path $repositoryRoot 'LICENSE') -Destination $stage
Copy-Item -LiteralPath (Join-Path $repositoryRoot 'THIRD_PARTY_NOTICES.md') -Destination $stage
Copy-Item -LiteralPath (Join-Path $repositoryRoot 'assets\web\vendor\LICENSE.lightweight-charts') -Destination $stage
Copy-Item -LiteralPath (Join-Path $repositoryRoot 'assets\web\vendor\NOTICE.lightweight-charts') -Destination $stage

$originalVcInstallDirectory = $env:VCINSTALLDIR
try {
    $env:VCINSTALLDIR = "$(Join-Path $visualStudioRoot 'VC')\"
    & (Join-Path $qtBin 'windeployqt.exe') `
        --release `
        --no-translations `
        --skip-plugin-types position,qmltooling,generic `
        --compiler-runtime `
        (Join-Path $stage 'tradingview_chart.exe')
    if ($LASTEXITCODE -ne 0) {
        throw "windeployqt failed with exit code $LASTEXITCODE."
    }
} finally {
    $env:VCINSTALLDIR = $originalVcInstallDirectory
}

Compress-Archive -LiteralPath $stage -DestinationPath $archive -CompressionLevel Optimal
$hash = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant()
Set-Content -LiteralPath "$archive.sha256" -Value "$hash  $(Split-Path $archive -Leaf)" -Encoding ascii

Write-Host "Created $archive"
Write-Host "SHA-256 $hash"
