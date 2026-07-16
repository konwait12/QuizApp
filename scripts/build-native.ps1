param(
    [ValidateSet('Windows', 'Android', 'All')]
    [string]$Target = 'All',
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Debug',
    [string]$QtRoot = $env:QUIZAPP_QT_ROOT,
    [string]$MinGWHome = $env:QUIZAPP_MINGW_HOME,
    [string]$AndroidSdkRoot = $env:ANDROID_SDK_ROOT,
    [string]$AndroidNdkRoot = $env:ANDROID_NDK_ROOT,
    [string]$JavaHome = $env:QUIZAPP_JAVA_HOME,
    [string]$CargoExecutable = $env:QUIZAPP_CARGO,
    [string]$AndroidPackageName = 'com.quizapp',
    [string]$AndroidAppName = 'QuizApp',
    [switch]$Clean,
    [switch]$SkipTests
)

$ErrorActionPreference = 'Stop'

$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$nativeRoot = Join-Path $projectRoot 'native'
$localToolRoot = Join-Path $nativeRoot '.toolchains\Qt'
$localPythonTools = Join-Path $nativeRoot '.venv-toolchain\Scripts'

function Resolve-Executable {
    param([string]$LocalPath, [string]$CommandName)

    if (Test-Path -LiteralPath $LocalPath) {
        return [IO.Path]::GetFullPath($LocalPath)
    }
    $command = Get-Command $CommandName -ErrorAction SilentlyContinue
    if (-not $command) {
        throw "Required executable not found: $CommandName"
    }
    return $command.Source
}

function Assert-ExistingDirectory {
    param([string]$Path, [string]$Label)

    if (-not $Path -or -not (Test-Path -LiteralPath $Path -PathType Container)) {
        throw "$Label not found: $Path"
    }
    return [IO.Path]::GetFullPath($Path)
}

function Remove-ProjectBuildDirectory {
    param([string]$Path)

    $resolved = [IO.Path]::GetFullPath($Path)
    $buildRoot = [IO.Path]::GetFullPath((Join-Path $nativeRoot 'build'))
    if (-not $resolved.StartsWith($buildRoot + [IO.Path]::DirectorySeparatorChar,
            [StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to remove a directory outside native/build: $resolved"
    }
    if (Test-Path -LiteralPath $resolved) {
        Remove-Item -LiteralPath $resolved -Recurse -Force
    }
}

function Reset-BuildIfSourceMappingChanged {
    param([string]$BuildDirectory, [string]$MappedSourceDirectory)

    $cachePath = Join-Path $BuildDirectory 'CMakeCache.txt'
    if (-not (Test-Path -LiteralPath $cachePath)) {
        return
    }
    $homeLine = Get-Content -LiteralPath $cachePath -Encoding UTF8 |
        Where-Object { $_.StartsWith('CMAKE_HOME_DIRECTORY:INTERNAL=') } |
        Select-Object -First 1
    if (-not $homeLine) {
        return
    }
    $cachedSource = ($homeLine -split '=', 2)[-1].Replace('\', '/').TrimEnd('/')
    $currentSource = $MappedSourceDirectory.Replace('\', '/').TrimEnd('/')
    if (-not $cachedSource.Equals($currentSource, [StringComparison]::OrdinalIgnoreCase)) {
        Write-Host "Build mapping changed from $cachedSource to $currentSource; regenerating."
        Remove-ProjectBuildDirectory $BuildDirectory
    }
}

function Invoke-Checked {
    param([string]$Executable, [string[]]$Arguments)

    & $Executable @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$Executable failed with exit code $LASTEXITCODE"
    }
}

function Find-AndroidNdk {
    param([string]$SdkRoot, [string]$ExplicitNdk)

    if ($ExplicitNdk) {
        return Assert-ExistingDirectory $ExplicitNdk 'Android NDK'
    }
    $ndkParent = Join-Path $SdkRoot 'ndk'
    if (-not (Test-Path -LiteralPath $ndkParent)) {
        throw 'Android NDK not found. Set ANDROID_NDK_ROOT.'
    }
    $candidate = Get-ChildItem -LiteralPath $ndkParent -Directory |
        Sort-Object { try { [version]$_.Name } catch { [version]'0.0' } } -Descending |
        Select-Object -First 1
    if (-not $candidate) {
        throw 'Android NDK not found. Set ANDROID_NDK_ROOT.'
    }
    return $candidate.FullName
}

function Find-AndroidJavaHome {
    param([string]$ExplicitJavaHome)

    $candidates = @()
    if ($ExplicitJavaHome) {
        $candidates += $ExplicitJavaHome
    }
    if ($env:WORKSPACE_RUNTIME_HOME) {
        $candidates += (Join-Path $env:WORKSPACE_RUNTIME_HOME 'Java\jdk-21')
    }
    if ($env:WORKSPACE_JAVA_HOME) {
        $candidates += $env:WORKSPACE_JAVA_HOME
    }
    if ($env:JAVA_HOME) {
        $candidates += $env:JAVA_HOME
    }

    foreach ($candidate in $candidates | Select-Object -Unique) {
        if ($candidate -and (Test-Path -LiteralPath (Join-Path $candidate 'bin\java.exe'))) {
            return [IO.Path]::GetFullPath($candidate)
        }
    }
    throw 'A Java runtime for Android packaging was not found. Set QUIZAPP_JAVA_HOME.'
}

function Find-CargoExecutable {
    param([string]$ExplicitCargo, [string]$CompilerHome)

    $candidates = @()
    if ($ExplicitCargo) {
        $candidates += $ExplicitCargo
    }
    if ($env:CARGO_HOME) {
        $candidates += (Join-Path $env:CARGO_HOME 'bin\cargo.exe')
    }
    $cargoCommand = Get-Command cargo -ErrorAction SilentlyContinue
    if ($cargoCommand) {
        $candidates += $cargoCommand.Source
    }
    if ($CompilerHome) {
        $toolRoot = Split-Path -Parent $CompilerHome
        $candidates += (Join-Path $toolRoot 'Rust\cargo\bin\cargo.exe')
    }
    foreach ($candidate in $candidates | Select-Object -Unique) {
        if ($candidate -and (Test-Path -LiteralPath $candidate -PathType Leaf)) {
            return [IO.Path]::GetFullPath($candidate)
        }
    }
    throw 'Cargo was not found. Set QUIZAPP_CARGO to an ASCII-only Rust toolchain path.'
}

$cmake = Resolve-Executable (Join-Path $localPythonTools 'cmake.exe') 'cmake'
$ninja = Resolve-Executable (Join-Path $localPythonTools 'ninja.exe') 'ninja'
$ctest = Resolve-Executable (Join-Path $localPythonTools 'ctest.exe') 'ctest'

if (-not $QtRoot) {
    $QtRoot = $localToolRoot
}
$QtRoot = Assert-ExistingDirectory $QtRoot 'Qt root'
$qtHost = Assert-ExistingDirectory (Join-Path $QtRoot '6.9.3\mingw_64') 'Qt 6.9.3 host'
$qtAndroid = Join-Path $QtRoot '6.9.3\android_arm64_v8a'

$mappedRoot = $projectRoot
$createdSubst = $false
$substDrive = $null
$substOutput = @(& subst)
if ($projectRoot -match '[^\x00-\x7F]') {
    foreach ($letter in @('Q:', 'R:', 'S:', 'T:', 'U:')) {
        $line = $substOutput | Where-Object { $_.StartsWith("$letter\", [StringComparison]::OrdinalIgnoreCase) } |
            Select-Object -First 1
        if ($line) {
            $mappedTarget = ($line -split '=>', 2)[-1].Trim()
            if ($mappedTarget -eq $projectRoot) {
                $substDrive = $letter
                break
            }
            continue
        }
        & subst $letter $projectRoot
        if ($LASTEXITCODE -ne 0) {
            continue
        }
        $substDrive = $letter
        $createdSubst = $true
        break
    }
    if (-not $substDrive) {
        throw 'No free drive letter is available for the ASCII build-path mapping.'
    }
    $mappedRoot = "$substDrive\"
}

function Convert-ToMappedPath {
    param([string]$Path)

    $absolute = [IO.Path]::GetFullPath($Path)
    if ($mappedRoot -eq $projectRoot -or
        -not $absolute.StartsWith($projectRoot, [StringComparison]::OrdinalIgnoreCase)) {
        return $absolute
    }
    $relative = $absolute.Substring($projectRoot.Length).TrimStart('\', '/')
    return Join-Path $mappedRoot $relative
}

try {
    $mappedNative = Convert-ToMappedPath $nativeRoot
    $mappedQtHost = Convert-ToMappedPath $qtHost
    $mappedQtAndroid = Convert-ToMappedPath $qtAndroid
    $mappedNinja = Convert-ToMappedPath $ninja

    if ($Target -in @('Windows', 'All')) {
        if (-not $MinGWHome) {
            $MinGWHome = Join-Path $QtRoot 'Tools\mingw1310_64'
        }
        $MinGWHome = Assert-ExistingDirectory $MinGWHome 'MinGW 13.1'
        if ($MinGWHome -match '[^\x00-\x7F]') {
            throw 'MinGW must be installed in an ASCII-only path. Set QUIZAPP_MINGW_HOME.'
        }
        $gpp = Join-Path $MinGWHome 'bin\g++.exe'
        if (-not (Test-Path -LiteralPath $gpp)) {
            throw "MinGW C++ compiler not found: $gpp"
        }
        $CargoExecutable = Find-CargoExecutable $CargoExecutable $MinGWHome
        $cargoHome = Split-Path -Parent (Split-Path -Parent $CargoExecutable)
        $rustRoot = Split-Path -Parent $cargoHome
        $cargoTargetDir = Join-Path (Split-Path -Parent $rustRoot) 'BuildCache\Project013\cargo'
        New-Item -ItemType Directory -Force -Path $cargoTargetDir | Out-Null

        $windowsBuild = Join-Path $nativeRoot ("build\windows-{0}" -f $Configuration.ToLowerInvariant())
        if ($Clean) {
            Remove-ProjectBuildDirectory $windowsBuild
        }
        $mappedWindowsBuild = Convert-ToMappedPath $windowsBuild
        Reset-BuildIfSourceMappingChanged $windowsBuild $mappedNative
        $oldPath = $env:PATH
        $oldCargoHome = $env:CARGO_HOME
        $oldRustupHome = $env:RUSTUP_HOME
        try {
            $env:CARGO_HOME = $cargoHome
            $env:RUSTUP_HOME = Join-Path $rustRoot 'rustup'
            $env:PATH = "$(Join-Path $MinGWHome 'bin');$mappedQtHost\bin;$oldPath"
            Invoke-Checked $cmake @(
                '-S', $mappedNative,
                '-B', $mappedWindowsBuild,
                '-G', 'Ninja',
                "-DCMAKE_MAKE_PROGRAM=$mappedNinja",
                "-DCMAKE_CXX_COMPILER=$gpp",
                "-DCMAKE_PREFIX_PATH=$mappedQtHost",
                "-DCMAKE_BUILD_TYPE=$Configuration",
                "-DQUIZAPP_CARGO_EXECUTABLE=$CargoExecutable",
                "-DQUIZAPP_CARGO_TARGET_DIR=$cargoTargetDir",
                '-DBUILD_TESTING=ON'
            )
            Invoke-Checked $cmake @('--build', $mappedWindowsBuild, '--parallel')
            if (-not $SkipTests) {
                Invoke-Checked $ctest @(
                    '--test-dir', $mappedWindowsBuild,
                    '--output-on-failure'
                )
            }
        }
        finally {
            $env:PATH = $oldPath
            $env:CARGO_HOME = $oldCargoHome
            $env:RUSTUP_HOME = $oldRustupHome
        }
    }

    if ($Target -in @('Android', 'All')) {
        $qtAndroid = Assert-ExistingDirectory $qtAndroid 'Qt 6.9.3 Android arm64'
        if (-not $AndroidSdkRoot) {
            $AndroidSdkRoot = $env:ANDROID_HOME
        }
        $AndroidSdkRoot = Assert-ExistingDirectory $AndroidSdkRoot 'Android SDK'
        $AndroidNdkRoot = Find-AndroidNdk $AndroidSdkRoot $AndroidNdkRoot
        $JavaHome = Find-AndroidJavaHome $JavaHome
        $CargoExecutable = Find-CargoExecutable $CargoExecutable $MinGWHome
        $cargoHome = Split-Path -Parent (Split-Path -Parent $CargoExecutable)
        $rustRoot = Split-Path -Parent $cargoHome
        $cargoTargetDir = Join-Path (Split-Path -Parent $rustRoot) 'BuildCache\Project013\cargo'
        New-Item -ItemType Directory -Force -Path $cargoTargetDir | Out-Null
        $androidToolchain = Join-Path $mappedQtAndroid 'lib\cmake\Qt6\qt.toolchain.cmake'
        if (-not (Test-Path -LiteralPath $androidToolchain)) {
            throw "Qt Android toolchain not found: $androidToolchain"
        }

        $androidBuild = Join-Path $nativeRoot ("build\android-arm64-{0}" -f $Configuration.ToLowerInvariant())
        if ($Clean) {
            Remove-ProjectBuildDirectory $androidBuild
        }
        $mappedAndroidBuild = Convert-ToMappedPath $androidBuild
        Reset-BuildIfSourceMappingChanged $androidBuild $mappedNative
        $oldJavaHome = $env:JAVA_HOME
        $oldPath = $env:PATH
        $oldCargoHome = $env:CARGO_HOME
        $oldRustupHome = $env:RUSTUP_HOME
        try {
            $env:JAVA_HOME = $JavaHome
            $env:CARGO_HOME = $cargoHome
            $env:RUSTUP_HOME = Join-Path $rustRoot 'rustup'
            $env:PATH = "$(Join-Path $JavaHome 'bin');$oldPath"
            Write-Host "Android Java runtime: $JavaHome"
            Invoke-Checked $cmake @(
                '-S', $mappedNative,
                '-B', $mappedAndroidBuild,
                '-G', 'Ninja',
                "-DCMAKE_MAKE_PROGRAM=$mappedNinja",
                "-DCMAKE_TOOLCHAIN_FILE=$androidToolchain",
                "-DQT_HOST_PATH=$mappedQtHost",
                "-DANDROID_SDK_ROOT=$AndroidSdkRoot",
                "-DANDROID_NDK_ROOT=$AndroidNdkRoot",
                '-DANDROID_ABI=arm64-v8a',
                '-DANDROID_PLATFORM=android-26',
                "-DQUIZAPP_ANDROID_PACKAGE_NAME=$AndroidPackageName",
                "-DQUIZAPP_ANDROID_APP_NAME=$AndroidAppName",
                "-DCMAKE_BUILD_TYPE=$Configuration",
                "-DQUIZAPP_CARGO_EXECUTABLE=$CargoExecutable",
                "-DQUIZAPP_CARGO_TARGET_DIR=$cargoTargetDir",
                '-DBUILD_TESTING=OFF'
            )
            Invoke-Checked $cmake @('--build', $mappedAndroidBuild, '--parallel')
        }
        finally {
            $env:JAVA_HOME = $oldJavaHome
            $env:PATH = $oldPath
            $env:CARGO_HOME = $oldCargoHome
            $env:RUSTUP_HOME = $oldRustupHome
        }

        $apkPath = Join-Path $androidBuild 'android-build\quizapp_native_app.apk'
        if (-not (Test-Path -LiteralPath $apkPath)) {
            throw "Android build completed without the expected APK: $apkPath"
        }
        $buildTools = Get-ChildItem -LiteralPath (Join-Path $AndroidSdkRoot 'build-tools') -Directory |
            Sort-Object { try { [version]$_.Name } catch { [version]'0.0' } } -Descending |
            Where-Object { Test-Path -LiteralPath (Join-Path $_.FullName 'aapt2.exe') } |
            Select-Object -First 1
        if (-not $buildTools) {
            throw 'Android aapt2 was not found; APK package identity cannot be verified.'
        }
        $badging = & (Join-Path $buildTools.FullName 'aapt2.exe') dump badging $apkPath
        if ($LASTEXITCODE -ne 0 -or -not ($badging | Select-String "package: name='$AndroidPackageName'")) {
            throw "Generated APK does not declare the expected $AndroidPackageName package."
        }

        $artifactRoot = Join-Path $projectRoot 'output\native-build'
        New-Item -ItemType Directory -Force -Path $artifactRoot | Out-Null
        $artifactName = "QuizApp-native-{0}-arm64-v8a.apk" -f $Configuration.ToLowerInvariant()
        $artifactPath = Join-Path $artifactRoot $artifactName
        Copy-Item -LiteralPath $apkPath -Destination $artifactPath -Force
        $artifactHash = (Get-FileHash -LiteralPath $artifactPath -Algorithm SHA256).Hash
        Write-Host "Android APK: $artifactPath"
        Write-Host "Android APK SHA256: $artifactHash"
    }

    Write-Host "QuizApp native $Target $Configuration build completed."
}
finally {
    if ($createdSubst -and $substDrive) {
        & subst $substDrive /d
    }
}
