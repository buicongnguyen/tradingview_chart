[CmdletBinding()]
param(
    [string]$QtVersion = '6.8.3',
    [ValidateSet('android_arm64_v8a', 'android_x86_64')]
    [string]$QtArchitecture = 'android_arm64_v8a',
    [ValidateSet('Release', 'Debug')]
    [string]$Configuration = 'Release',
    [string]$AndroidSdkRoot =
        (Join-Path $env:LOCALAPPDATA 'Android\Sdk'),
    [string]$NdkVersion = '26.1.10909125',
    [string]$BuildDirectory = '',
    [switch]$SkipSigning,
    [switch]$InitializeSigningKey
)

$ErrorActionPreference = 'Stop'

function Find-VisualStudioTool {
    param(
        [Parameter(Mandatory)]
        [string]$RelativePath
    )

    $vswhere = Join-Path ${env:ProgramFiles(x86)} `
        'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path -LiteralPath $vswhere)) {
        return ''
    }
    $visualStudioRoot = (& $vswhere `
        -latest `
        -products '*' `
        -property installationPath | Select-Object -First 1)
    if ([string]::IsNullOrWhiteSpace($visualStudioRoot)) {
        return ''
    }
    $candidate = Join-Path $visualStudioRoot $RelativePath
    if (Test-Path -LiteralPath $candidate) {
        return $candidate
    }
    return ''
}

$repositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
if ([string]::IsNullOrWhiteSpace($BuildDirectory)) {
    $architectureSuffix = if ($QtArchitecture -eq 'android_arm64_v8a') {
        'arm64'
    } else {
        'x86_64'
    }
    $BuildDirectory = Join-Path $repositoryRoot `
        "build-android-$architectureSuffix-$($Configuration.ToLowerInvariant())"
}

$qtRoot = Join-Path $repositoryRoot ".qt\$QtVersion\$QtArchitecture"
$qtHostRoot = Join-Path $repositoryRoot ".qt\$QtVersion\msvc2022_64"
$qtCmake = Join-Path $qtRoot 'bin\qt-cmake.bat'
$ndkRoot = Join-Path $AndroidSdkRoot "ndk\$NdkVersion"
if (-not (Test-Path -LiteralPath $qtCmake)) {
    throw "Qt for Android was not found. Run scripts\bootstrap-android.ps1 first."
}
if (-not (Test-Path -LiteralPath (Join-Path $qtHostRoot 'bin\moc.exe'))) {
    throw "The Qt $QtVersion Windows host tools were not found at $qtHostRoot."
}
if (-not (Test-Path -LiteralPath (Join-Path $ndkRoot 'source.properties'))) {
    throw "Android NDK $NdkVersion was not found under $AndroidSdkRoot."
}

$cmake = (Get-Command cmake -ErrorAction SilentlyContinue).Source
if ([string]::IsNullOrWhiteSpace($cmake)) {
    $cmake = Find-VisualStudioTool `
        'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
}
$ninja = (Get-Command ninja -ErrorAction SilentlyContinue).Source
if ([string]::IsNullOrWhiteSpace($ninja)) {
    $ninja = Find-VisualStudioTool `
        'Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe'
}
if ([string]::IsNullOrWhiteSpace($cmake) -or
    -not (Test-Path -LiteralPath $cmake)) {
    throw 'CMake was not found.'
}
if ([string]::IsNullOrWhiteSpace($ninja) -or
    -not (Test-Path -LiteralPath $ninja)) {
    throw 'Ninja was not found.'
}

$cmakeDirectory = Split-Path -Parent $cmake
$ninjaDirectory = Split-Path -Parent $ninja
$env:PATH = "$cmakeDirectory;$ninjaDirectory;$env:PATH"
$env:ANDROID_SDK_ROOT = $AndroidSdkRoot
$env:ANDROID_NDK_ROOT = $ndkRoot

$androidAbi = if ($QtArchitecture -eq 'android_arm64_v8a') {
    'arm64-v8a'
} else {
    'x86_64'
}

& $qtCmake `
    -S $repositoryRoot `
    -B $BuildDirectory `
    -G Ninja `
    "-DCMAKE_BUILD_TYPE=$Configuration" `
    "-DANDROID_SDK_ROOT=$AndroidSdkRoot" `
    "-DANDROID_NDK_ROOT=$ndkRoot" `
    "-DQT_HOST_PATH=$qtHostRoot" `
    "-DANDROID_ABI=$androidAbi" `
    '-DANDROID_PLATFORM=android-28' `
    '-DTRADINGVIEW_CHART_BUILD_TESTS=OFF'
if ($LASTEXITCODE -ne 0) {
    throw "Android CMake configure failed with exit code $LASTEXITCODE."
}

& $cmake --build $BuildDirectory --target apk --parallel
if ($LASTEXITCODE -ne 0) {
    throw "Android APK build failed with exit code $LASTEXITCODE."
}

$unsignedApk = Get-ChildItem -LiteralPath $BuildDirectory -Filter '*.apk' `
    -Recurse |
    Where-Object { $_.Name -match 'release-unsigned\.apk$' } |
    Select-Object -First 1
if ($null -eq $unsignedApk) {
    $unsignedApk = Get-ChildItem -LiteralPath $BuildDirectory -Filter '*.apk' `
        -Recurse |
        Select-Object -First 1
}
if ($null -eq $unsignedApk) {
    throw "No APK was produced under $BuildDirectory."
}

$distRoot = Join-Path $repositoryRoot 'dist'
New-Item -ItemType Directory -Path $distRoot -Force | Out-Null
$packageBase = "TradingViewChart-0.8.0-android-$androidAbi"
$artifactSuffix = if ($Configuration -eq 'Debug') {
    '-debug'
} elseif ($SkipSigning) {
    '-unsigned'
} else {
    ''
}
$artifactBase = "$packageBase$artifactSuffix"
$outputApk = Join-Path $distRoot "$artifactBase.apk"

if ($Configuration -eq 'Release' -and -not $SkipSigning) {
    $signingRoot = Join-Path $env:LOCALAPPDATA 'TradingViewChart\signing'
    $keystore = Join-Path $signingRoot 'release.keystore'
    $passwordFile = Join-Path $signingRoot 'release-password.xml'
    New-Item -ItemType Directory -Path $signingRoot -Force | Out-Null

    $hasKeystore = Test-Path -LiteralPath $keystore
    $hasPasswordFile = Test-Path -LiteralPath $passwordFile
    if ($hasKeystore -xor $hasPasswordFile) {
        throw "Android signing material is incomplete under $signingRoot. Restore both files from the private backup."
    }
    if (-not $hasKeystore) {
        if (-not $InitializeSigningKey) {
            throw "Android signing material is missing. Restore the existing key, or explicitly pass -InitializeSigningKey only for a brand-new application identity."
        }

        $passwordBytes = New-Object byte[] 32
        [Security.Cryptography.RandomNumberGenerator]::Fill($passwordBytes)
        $password = [Convert]::ToBase64String($passwordBytes)

        $keytool = ''
        if (-not [string]::IsNullOrWhiteSpace($env:JAVA_HOME)) {
            $keytoolCandidate = Join-Path $env:JAVA_HOME 'bin\keytool.exe'
            if (Test-Path -LiteralPath $keytoolCandidate) {
                $keytool = $keytoolCandidate
            }
        }
        if ([string]::IsNullOrWhiteSpace($keytool)) {
            $keytool = (Get-Command keytool -ErrorAction Stop).Source
        }

        $temporaryId = [Guid]::NewGuid().ToString('N')
        $temporaryKeystore = Join-Path $signingRoot "$temporaryId.keystore.tmp"
        $temporaryPasswordFile = Join-Path $signingRoot "$temporaryId.password.tmp"
        try {
            ConvertTo-SecureString $password -AsPlainText -Force |
                Export-Clixml -LiteralPath $temporaryPasswordFile
            $env:TRADINGVIEW_CHART_SIGNING_PASSWORD = $password
            & $keytool -genkeypair -noprompt `
                -keystore $temporaryKeystore `
                -storepass:env TRADINGVIEW_CHART_SIGNING_PASSWORD `
                -keypass:env TRADINGVIEW_CHART_SIGNING_PASSWORD `
                -alias tradingview_chart `
                -keyalg RSA `
                -keysize 4096 `
                -validity 10000 `
                -dname 'CN=TradingView Chart, OU=Personal, O=buicongnguyen, C=US'
            if ($LASTEXITCODE -ne 0) {
                throw "Android signing key generation failed with exit code $LASTEXITCODE."
            }
            Move-Item -LiteralPath $temporaryPasswordFile `
                -Destination $passwordFile
            Move-Item -LiteralPath $temporaryKeystore -Destination $keystore
        } finally {
            Remove-Item Env:TRADINGVIEW_CHART_SIGNING_PASSWORD `
                -ErrorAction SilentlyContinue
            Remove-Item -LiteralPath $temporaryKeystore `
                -Force -ErrorAction SilentlyContinue
            Remove-Item -LiteralPath $temporaryPasswordFile `
                -Force -ErrorAction SilentlyContinue
            $password = $null
        }
    } elseif ($InitializeSigningKey) {
        throw "Signing material already exists. -InitializeSigningKey cannot replace the established application identity."
    }

    $securePassword = Import-Clixml -LiteralPath $passwordFile
    $passwordPointer = [Runtime.InteropServices.Marshal]::SecureStringToBSTR(
        $securePassword)
    try {
        $password = [Runtime.InteropServices.Marshal]::PtrToStringBSTR(
            $passwordPointer)
        $buildTools = Get-ChildItem `
            -LiteralPath (Join-Path $AndroidSdkRoot 'build-tools') `
            -Directory |
            Sort-Object { [version]$_.Name } -Descending |
            Select-Object -First 1
        $apksigner = Join-Path $buildTools.FullName 'apksigner.bat'
        if (-not (Test-Path -LiteralPath $apksigner)) {
            throw 'Android apksigner was not found.'
        }
        $env:TRADINGVIEW_CHART_SIGNING_PASSWORD = $password
        & $apksigner sign `
            --ks $keystore `
            --ks-key-alias tradingview_chart `
            --ks-pass 'env:TRADINGVIEW_CHART_SIGNING_PASSWORD' `
            --key-pass 'env:TRADINGVIEW_CHART_SIGNING_PASSWORD' `
            --out $outputApk `
            $unsignedApk.FullName
        if ($LASTEXITCODE -ne 0) {
            throw "APK signing failed with exit code $LASTEXITCODE."
        }
        & $apksigner verify --verbose --print-certs $outputApk
        if ($LASTEXITCODE -ne 0) {
            throw "APK signature verification failed with exit code $LASTEXITCODE."
        }
    } finally {
        Remove-Item Env:TRADINGVIEW_CHART_SIGNING_PASSWORD `
            -ErrorAction SilentlyContinue
        if ($passwordPointer -ne [IntPtr]::Zero) {
            [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($passwordPointer)
        }
        $password = $null
    }
} else {
    Copy-Item -LiteralPath $unsignedApk.FullName -Destination $outputApk -Force
}

$hash = (Get-FileHash -Algorithm SHA256 -LiteralPath $outputApk).Hash.ToLowerInvariant()
$checksum = Join-Path $distRoot "$artifactBase.apk.sha256"
Set-Content -LiteralPath $checksum -NoNewline `
    -Value "$hash  $artifactBase.apk"

Write-Host "Android package ready:"
Write-Host "  APK:    $outputApk"
Write-Host "  SHA256: $hash"
