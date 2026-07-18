param(
    [string]$ApkPath = 'output/native-build/QuizApp-native-debug-arm64-v8a.apk',
    [string]$DeviceSerial,
    [string]$PackageName = 'com.quizapp',
    [string]$ActivityName = 'org.quizapp.platform.QuizAppActivity',
    [ValidateRange(30, 600)]
    [int]$StartupTimeoutSeconds = 180
)

$ErrorActionPreference = 'Stop'

$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$resolvedApk = [IO.Path]::GetFullPath((Join-Path $projectRoot $ApkPath))
if (-not (Test-Path -LiteralPath $resolvedApk -PathType Leaf)) {
    throw "APK not found: $resolvedApk"
}

$androidSdkRoot = $env:ANDROID_SDK_ROOT
if (-not $androidSdkRoot) {
    $androidSdkRoot = $env:ANDROID_HOME
}
if (-not $androidSdkRoot) {
    throw 'ANDROID_SDK_ROOT is not configured.'
}
$adb = Join-Path $androidSdkRoot 'platform-tools/adb.exe'
if (-not (Test-Path -LiteralPath $adb -PathType Leaf)) {
    throw "adb not found under ANDROID_SDK_ROOT: $adb"
}

function Invoke-Adb {
    param([string[]]$Arguments, [switch]$AllowFailure)

    $commandArguments = @()
    if ($DeviceSerial) {
        $commandArguments += @('-s', $DeviceSerial)
    }
    $commandArguments += $Arguments
    $previousErrorActionPreference = $ErrorActionPreference
    try {
        # adb writes successful transfer progress to stderr. Capture it without
        # letting Windows PowerShell promote those lines to terminating errors.
        $ErrorActionPreference = 'Continue'
        $output = & $adb @commandArguments 2>&1
        $exitCode = $LASTEXITCODE
    }
    finally {
        $ErrorActionPreference = $previousErrorActionPreference
    }
    if ($exitCode -ne 0 -and -not $AllowFailure) {
        throw "adb $($Arguments -join ' ') failed:`n$($output -join [Environment]::NewLine)"
    }
    return @($output)
}

$deviceLines = & $adb devices
$onlineDevices = @(
    $deviceLines |
        Select-Object -Skip 1 |
        ForEach-Object {
            $parts = $_ -split '\s+'
            if ($parts.Count -ge 2 -and $parts[1] -eq 'device') {
                $parts[0]
            }
        }
)
if ($DeviceSerial) {
    if ($onlineDevices -notcontains $DeviceSerial) {
        throw "Android device is not online: $DeviceSerial"
    }
} elseif ($onlineDevices.Count -eq 1) {
    $DeviceSerial = $onlineDevices[0]
} elseif ($onlineDevices.Count -eq 0) {
    throw 'No online Android device is available.'
} else {
    throw 'Multiple Android devices are online. Pass -DeviceSerial explicitly.'
}

$outputRoot = Join-Path $projectRoot 'output/test-runtime/android-device'
New-Item -ItemType Directory -Force -Path $outputRoot | Out-Null
$timestamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$logPath = Join-Path $outputRoot "startup-$timestamp.log"
$screenshotPath = Join-Path $outputRoot "startup-$timestamp.png"

$deviceAbiOutput = @(Invoke-Adb @('shell', 'getprop', 'ro.product.cpu.abi'))
$deviceAbi = ([string]$deviceAbiOutput[-1]).Trim()
$expectedAbi = if ($resolvedApk -match 'x86_64') { 'x86_64' } else { 'arm64-v8a' }
if ($deviceAbi -ne $expectedAbi) {
    throw "APK ABI $expectedAbi does not match device ABI $deviceAbi."
}

Write-Host "Installing $resolvedApk on $DeviceSerial ($deviceAbi)"
Invoke-Adb @('install', '-r', '-t', $resolvedApk) | Write-Host
Invoke-Adb @('logcat', '-c') | Out-Null
Invoke-Adb @('shell', 'am', 'force-stop', $PackageName) | Out-Null

$component = "$PackageName/$ActivityName"
$launchOutput = Invoke-Adb @('shell', 'am', 'start', '-W', '-n', $component)
$launchOutput | Write-Host

$deadline = [DateTime]::UtcNow.AddSeconds($StartupTimeoutSeconds)
$appPid = ''
$mainWindowReady = $false
$fatalLines = @()
do {
    Start-Sleep -Milliseconds 500
    $pidOutput = Invoke-Adb @('shell', 'pidof', $PackageName) -AllowFailure
    $appPid = ($pidOutput -join '').Trim()
    $logcat = @(Invoke-Adb @('logcat', '-d', '-v', 'threadtime'))
    $fatalLines = @($logcat | Select-String -Pattern `
        'FATAL EXCEPTION|Fatal signal|UnsatisfiedLinkError|Process: com\.quizapp|ANR in com\.quizapp')
    $mainWindowReady = [bool]($logcat | Select-String -SimpleMatch `
        'QuizApp startup: main window shown' | Select-Object -First 1)
} while (-not $mainWindowReady -and $fatalLines.Count -eq 0 -and $appPid `
    -and [DateTime]::UtcNow -lt $deadline)

[IO.File]::WriteAllLines($logPath, $logcat, [Text.UTF8Encoding]::new($false))
if ($fatalLines.Count -gt 0) {
    throw "QuizApp emitted a fatal Android error. Full log: $logPath"
}
if (-not $appPid) {
    throw "QuizApp exited before its main window was ready. Full log: $logPath"
}
if (-not $mainWindowReady) {
    throw "QuizApp did not finish first-launch initialization within $StartupTimeoutSeconds seconds. Full log: $logPath"
}

Start-Sleep -Seconds 1
$appPid = ((Invoke-Adb @('shell', 'pidof', $PackageName) -AllowFailure) -join '').Trim()
if (-not $appPid) {
    throw "QuizApp exited immediately after showing its main window. Full log: $logPath"
}

$topActivity = Invoke-Adb @('shell', 'dumpsys', 'activity', 'activities') |
    Select-String -Pattern 'mResumedActivity|topResumedActivity' |
    Select-Object -First 1
if (-not $topActivity -or $topActivity.Line -notmatch [regex]::Escape($PackageName)) {
    throw "QuizApp process is running but its Activity is not resumed. Full log: $logPath"
}

$remoteScreenshot = '/sdcard/Download/quizapp-startup.png'
Invoke-Adb @('shell', 'screencap', '-p', $remoteScreenshot) | Out-Null
Invoke-Adb @('pull', $remoteScreenshot, $screenshotPath) | Out-Null
Invoke-Adb @('shell', 'rm', '-f', $remoteScreenshot) -AllowFailure | Out-Null

Write-Host "PASS: QuizApp main window is running with PID $appPid"
Write-Host "Log: $logPath"
Write-Host "Screenshot: $screenshotPath"
