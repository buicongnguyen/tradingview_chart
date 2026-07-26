[CmdletBinding()]
param(
    [ValidateSet('Release')]
    [string]$Configuration = 'Release',
    [string]$QtRoot = '',
    [ValidatePattern('^[A-Za-z0-9._-]+$')]
    [string]$PackageName = 'TradeChartLab-1.2.0-win64'
)

$ErrorActionPreference = 'Stop'

$repositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
if ([string]::IsNullOrWhiteSpace($QtRoot)) {
    $QtRoot = Join-Path $repositoryRoot '.qt\6.8.3\msvc2022_64'
}
$buildDirectory = Join-Path $repositoryRoot 'build-release'
$distRoot = Join-Path $repositoryRoot 'dist'
$stage = Join-Path $distRoot $PackageName
$archive = "$stage.zip"

& (Join-Path $PSScriptRoot 'configure-windows.ps1') `
    -Configuration $Configuration `
    -QtRoot $QtRoot `
    -BuildDirectory $buildDirectory

$cmakeCommand = Get-Command cmake -ErrorAction SilentlyContinue
$cmake = if ($null -ne $cmakeCommand) { $cmakeCommand.Source } else { '' }
$visualStudioRoot = ''
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (Test-Path -LiteralPath $vswhere) {
    $visualStudioRoot = (& $vswhere `
        -latest `
        -products '*' `
        -version '[17.0,18.0)' `
        -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
        -property installationPath | Select-Object -First 1)
}
if ([string]::IsNullOrWhiteSpace($cmake) -and
    -not [string]::IsNullOrWhiteSpace($visualStudioRoot)) {
    $candidate = Join-Path $visualStudioRoot 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
    if (Test-Path -LiteralPath $candidate) {
        $cmake = $candidate
    }
}
if ([string]::IsNullOrWhiteSpace($cmake) -or -not (Test-Path -LiteralPath $cmake)) {
    throw 'CMake was not found in PATH or a Visual Studio 2022 C++ installation.'
}
$ctest = Join-Path (Split-Path $cmake -Parent) 'ctest.exe'
if (-not (Test-Path -LiteralPath $ctest)) {
    $ctestCommand = Get-Command ctest -ErrorAction Stop
    $ctest = $ctestCommand.Source
}

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

$resolvedRepository = [System.IO.Path]::GetFullPath($repositoryRoot)
$resolvedDist = [System.IO.Path]::GetFullPath($distRoot)
if (-not $resolvedDist.StartsWith(
        "$resolvedRepository\",
        [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "Refusing to replace output paths outside the repository."
}
New-Item -ItemType Directory -Path $distRoot -Force | Out-Null
foreach ($replaceTarget in @($stage, $archive, "$archive.sha256")) {
    $resolvedTarget = [System.IO.Path]::GetFullPath($replaceTarget)
    if (-not $resolvedTarget.StartsWith(
            "$resolvedDist\",
            [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to replace an output outside the dist directory."
    }
    if (Test-Path -LiteralPath $replaceTarget) {
        Remove-Item -LiteralPath $replaceTarget -Recurse -Force
    }
}
New-Item -ItemType Directory -Path $stage | Out-Null

Copy-Item -LiteralPath (Join-Path $buildDirectory "$Configuration\tradingview_chart.exe") -Destination $stage
Copy-Item -LiteralPath (Join-Path $repositoryRoot 'LICENSE') -Destination $stage
Copy-Item -LiteralPath (Join-Path $repositoryRoot 'THIRD_PARTY_NOTICES.md') -Destination $stage
Copy-Item -LiteralPath (Join-Path $repositoryRoot 'assets\web\vendor\LICENSE.lightweight-charts') -Destination $stage
Copy-Item -LiteralPath (Join-Path $repositoryRoot 'assets\web\vendor\NOTICE.lightweight-charts') -Destination $stage
Copy-Item -LiteralPath (Join-Path $repositoryRoot 'examples\pine\licenses\opmau-MIT.txt') -Destination $stage
Copy-Item -LiteralPath (Join-Path $repositoryRoot 'examples\pine\licenses\eterna-MIT.txt') -Destination $stage

$originalVcInstallDirectory = $env:VCINSTALLDIR
try {
    if (-not [string]::IsNullOrWhiteSpace($visualStudioRoot)) {
        $env:VCINSTALLDIR = "$(Join-Path $visualStudioRoot 'VC')\"
    }
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

$requiredRuntimeFiles = @(
    'Qt6WebChannel.dll',
    'Qt6WebEngineCore.dll',
    'Qt6WebEngineWidgets.dll',
    'Qt6Network.dll',
    'Qt6Sql.dll',
    'QtWebEngineProcess.exe',
    'platforms\qwindows.dll',
    'sqldrivers\qsqlite.dll',
    'tls\qschannelbackend.dll',
    'resources\icudtl.dat'
)
foreach ($relativePath in $requiredRuntimeFiles) {
    if (-not (Test-Path -LiteralPath (Join-Path $stage $relativePath))) {
        throw "The deployed package is missing $relativePath."
    }
}

$originalSmokePath = $env:PATH
$originalChromiumFlags = $env:QTWEBENGINE_CHROMIUM_FLAGS
$originalQtOpenGl = $env:QT_OPENGL
$originalQtQuickBackend = $env:QT_QUICK_BACKEND
try {
    $env:PATH = "$env:SystemRoot\System32;$env:SystemRoot"
    $env:QTWEBENGINE_CHROMIUM_FLAGS =
        '--disable-gpu --disable-gpu-compositing --no-sandbox'
    $env:QT_OPENGL = 'software'
    $env:QT_QUICK_BACKEND = 'software'
    $smokeProcess = Start-Process `
        -FilePath (Join-Path $stage 'tradingview_chart.exe') `
        -ArgumentList '--smoke-test' `
        -WindowStyle Hidden `
        -Wait `
        -PassThru
    if ($smokeProcess.ExitCode -ne 0) {
        throw "The deployed application smoke test failed with exit code $($smokeProcess.ExitCode)."
    }
} finally {
    $env:PATH = $originalSmokePath
    $env:QTWEBENGINE_CHROMIUM_FLAGS = $originalChromiumFlags
    $env:QT_OPENGL = $originalQtOpenGl
    $env:QT_QUICK_BACKEND = $originalQtQuickBackend
}

Compress-Archive -LiteralPath $stage -DestinationPath $archive -CompressionLevel Optimal
$hash = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant()
Set-Content -LiteralPath "$archive.sha256" -Value "$hash  $(Split-Path $archive -Leaf)" -Encoding ascii

Write-Host "Created $archive"
Write-Host "SHA-256 $hash"
