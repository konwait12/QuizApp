param(
    [string]$Version = "v1.0.20",
  [string]$AppVersion = "",
  [int]$VersionCode = 0,
  [string]$BuildCommit = "",
  [switch]$IncludePostgraduateBanks
)

$ErrorActionPreference = "Stop"

$projectRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$workspaceRoot = Resolve-Path (Join-Path $projectRoot "..\..")
$runtimeScript = Resolve-Path (Join-Path $workspaceRoot "Runtimes\scripts\Use-WorkspaceRuntimes.ps1")
. $runtimeScript -Quiet

$java21Home = Join-Path $workspaceRoot "Runtimes\Java\jdk-21"
if (Test-Path -LiteralPath (Join-Path $java21Home "bin\javac.exe")) {
  $env:JAVA_HOME = $java21Home
  $env:PATH = (Join-Path $java21Home "bin") + ";" + $env:PATH
}

$androidSdk = $env:ANDROID_SDK_ROOT
if (-not $androidSdk) { $androidSdk = $env:ANDROID_HOME }
if (-not $androidSdk -or -not (Test-Path -LiteralPath $androidSdk)) {
  throw "Android SDK not found. Set ANDROID_HOME or ANDROID_SDK_ROOT."
}

$buildTools = Join-Path $androidSdk "build-tools\34.0.0"
$platformJar = Join-Path $androidSdk "platforms\android-34\android.jar"
$aapt2 = Join-Path $buildTools "aapt2.exe"
$d8 = Join-Path $buildTools "d8.bat"
$zipalign = Join-Path $buildTools "zipalign.exe"
$apksigner = Join-Path $buildTools "apksigner.bat"
$keytool = Join-Path $env:JAVA_HOME "bin\keytool.exe"
$javac = Join-Path $env:JAVA_HOME "bin\javac.exe"
$jar = Join-Path $env:JAVA_HOME "bin\jar.exe"

foreach ($tool in @($aapt2, $d8, $zipalign, $apksigner, $keytool, $javac, $jar, $platformJar)) {
  if (-not (Test-Path -LiteralPath $tool)) { throw "Required tool not found: $tool" }
}

$buildRoot = $projectRoot.Path
$substDrive = "Q:"
$substCreated = $false
if ($projectRoot.Path -match "[^\x00-\x7F]") {
  $existing = (& subst) | Where-Object { $_ -like "$substDrive\:*" }
  if ($existing) {
    & subst $substDrive /D | Out-Null
  }
  & subst $substDrive $projectRoot.Path
  if ($LASTEXITCODE -ne 0) { throw "Failed to create temporary build drive $substDrive" }
  $substCreated = $true
  $buildRoot = "$substDrive\"
}

$androidDir = Join-Path $buildRoot "android"
$buildDir = Join-Path $buildRoot "output\apk-build"
$assetDir = Join-Path $buildDir "assets"
$classesDir = Join-Path $buildDir "classes"
$dexDir = Join-Path $buildDir "dex"
$unsignedApk = Join-Path $buildDir "quizapp-unsigned.apk"
$classesApk = Join-Path $buildDir "quizapp-classes.apk"
$alignedApk = Join-Path $buildDir "quizapp-aligned.apk"
$finalApk = Join-Path $projectRoot "output\QuizApp-$Version-debug.apk"
$keystore = Join-Path $projectRoot "output\quizapp-debug.keystore"
$manifestFile = Join-Path $buildDir "AndroidManifest.xml"
$resolvedAppVersion = if ($AppVersion) { $AppVersion } else { $Version }
$resolvedVersionName = $resolvedAppVersion.TrimStart("v")
$buildCommit = if ($BuildCommit) { $BuildCommit.Trim() } else { "dev" }
if (-not $BuildCommit) {
  try {
    $gitCommit = (& git -C $projectRoot rev-parse HEAD 2>$null)
    if ($LASTEXITCODE -eq 0 -and $gitCommit) {
      $buildCommit = ($gitCommit | Select-Object -First 1).Trim()
    }
  } catch {
    $buildCommit = "dev"
  }
}
if (-not $VersionCode -or $VersionCode -lt 1) {
  $versionMatch = [regex]::Match($resolvedVersionName, "(\d+)\.(\d+)\.(\d+)")
  if ($versionMatch.Success) {
    $VersionCode = [int]$versionMatch.Groups[1].Value * 10000 + [int]$versionMatch.Groups[2].Value * 100 + [int]$versionMatch.Groups[3].Value
  } else {
    $VersionCode = 1
  }
}

if (Test-Path -LiteralPath $buildDir) { Remove-Item -LiteralPath $buildDir -Recurse -Force }
New-Item -ItemType Directory -Force -Path $assetDir, $classesDir, $dexDir | Out-Null

Copy-Item -LiteralPath (Join-Path $projectRoot "index.html") -Destination (Join-Path $assetDir "index.html")
$sourceVendorDir = Join-Path $projectRoot "vendor"
if (Test-Path -LiteralPath $sourceVendorDir) {
  Copy-Item -LiteralPath $sourceVendorDir -Destination (Join-Path $assetDir "vendor") -Recurse -Force
}
$sourceNotebookDir = Join-Path $projectRoot "notebook"
if (Test-Path -LiteralPath $sourceNotebookDir) {
  Copy-Item -LiteralPath $sourceNotebookDir -Destination (Join-Path $assetDir "notebook") -Recurse -Force
}
$sourceShellDir = Join-Path $projectRoot "shell"
if (Test-Path -LiteralPath $sourceShellDir) {
  Copy-Item -LiteralPath $sourceShellDir -Destination (Join-Path $assetDir "shell") -Recurse -Force
}
$sourceReviewDir = Join-Path $projectRoot "review"
if (Test-Path -LiteralPath $sourceReviewDir) {
  Copy-Item -LiteralPath $sourceReviewDir -Destination (Join-Path $assetDir "review") -Recurse -Force
}
$sourceExamDir = Join-Path $projectRoot "exam"
if (Test-Path -LiteralPath $sourceExamDir) {
  Copy-Item -LiteralPath $sourceExamDir -Destination (Join-Path $assetDir "exam") -Recurse -Force
}
$sourceAiDir = Join-Path $projectRoot "ai"
if (Test-Path -LiteralPath $sourceAiDir) {
  Copy-Item -LiteralPath $sourceAiDir -Destination (Join-Path $assetDir "ai") -Recurse -Force
}
$sourceBackupDir = Join-Path $projectRoot "backup"
if (Test-Path -LiteralPath $sourceBackupDir) {
  Copy-Item -LiteralPath $sourceBackupDir -Destination (Join-Path $assetDir "backup") -Recurse -Force
}
$sourceDistributionDir = Join-Path $projectRoot "distribution"
if (Test-Path -LiteralPath $sourceDistributionDir) {
  Copy-Item -LiteralPath $sourceDistributionDir -Destination (Join-Path $assetDir "distribution") -Recurse -Force
}
$assetDataDir = Join-Path $assetDir "data"
New-Item -ItemType Directory -Force -Path $assetDataDir | Out-Null
$assetBankFiles = @()
$sourceDataRoot = Join-Path $projectRoot "data"
$sourceDataRootFull = (Resolve-Path -LiteralPath $sourceDataRoot).Path.TrimEnd("\", "/")
$sourceBankFiles = Get-ChildItem -LiteralPath $sourceDataRoot -Filter "*.json" -Recurse | Sort-Object FullName
$bankAssetSources = @($sourceBankFiles | ForEach-Object {
  [pscustomobject]@{
    File = $_
    RelativePath = $_.FullName.Substring($sourceDataRootFull.Length).TrimStart("\", "/").Replace("\", "/")
  }
})
$postgraduateRoot = Join-Path $projectRoot "output\xiaoyi-question-banks"
$postgraduateReport = Join-Path $postgraduateRoot "export-report.json"
if ($IncludePostgraduateBanks -and (Test-Path -LiteralPath $postgraduateReport)) {
  $postgraduateRootFull = (Resolve-Path -LiteralPath $postgraduateRoot).Path.TrimEnd("\", "/")
  $postgraduateFiles = Get-ChildItem -LiteralPath $postgraduateRoot -Filter "*.json" -Recurse -File |
    Where-Object { $_.Name -ne "export-report.json" } |
    Sort-Object FullName
  $bankAssetSources += @($postgraduateFiles | ForEach-Object {
    $relative = $_.FullName.Substring($postgraduateRootFull.Length).TrimStart("\", "/").Replace("\", "/")
    [pscustomobject]@{ File = $_; RelativePath = "postgraduate/$relative" }
  })
}
for ($i = 0; $i -lt $bankAssetSources.Count; $i++) {
  $relativePath = $bankAssetSources[$i].RelativePath
  $assetTarget = Join-Path $assetDataDir ($relativePath.Replace("/", [System.IO.Path]::DirectorySeparatorChar))
  $assetTargetDir = Split-Path -Parent $assetTarget
  if (-not (Test-Path -LiteralPath $assetTargetDir)) {
    New-Item -ItemType Directory -Force -Path $assetTargetDir | Out-Null
  }
  Copy-Item -LiteralPath $bankAssetSources[$i].File.FullName -Destination $assetTarget
  $assetBankFiles += $relativePath
}

$assetIndex = Join-Path $assetDir "index.html"
$indexText = [System.IO.File]::ReadAllText($assetIndex, [System.Text.Encoding]::UTF8)
$bankList = ($assetBankFiles | ForEach-Object { "  '$($_)'" }) -join ",`n"
$indexText = [regex]::Replace(
  $indexText,
  "const DEFAULT_BANK_FILES = \[[\s\S]*?\];",
  "const DEFAULT_BANK_FILES = [`n$bankList,`n];"
)
$indexText = [regex]::Replace(
  $indexText,
  "const DEFAULT_BANK_FILES_RUNTIME = \[[\s\S]*?\];",
  "const DEFAULT_BANK_FILES_RUNTIME = [`n$bankList,`n];"
)
$embeddedBanks = @()
foreach ($sourceBankFile in $sourceBankFiles) {
  $embeddedBanks += [System.IO.File]::ReadAllText($sourceBankFile.FullName, [System.Text.Encoding]::UTF8) | ConvertFrom-Json
}
$embeddedJson = $embeddedBanks | ConvertTo-Json -Depth 100 -Compress
$indexText = $indexText.Replace("window.__QUIZAPP_EMBEDDED_BANKS__ = null;", "window.__QUIZAPP_EMBEDDED_BANKS__ = $embeddedJson;")
$indexText = [regex]::Replace($indexText, "const APP_VERSION = 'v[^']+';", "const APP_VERSION = '$resolvedAppVersion';")
$indexText = [regex]::Replace($indexText, "const APP_BUILD_COMMIT = '[^']*';", "const APP_BUILD_COMMIT = '$buildCommit';")
[System.IO.File]::WriteAllText($assetIndex, $indexText, [System.Text.UTF8Encoding]::new($false))

$copiedBankCount = (Get-ChildItem -LiteralPath $assetDataDir -Filter "*.json" -Recurse -File).Count
if ($bankAssetSources.Count -lt 1 -or $copiedBankCount -ne $bankAssetSources.Count) {
  throw "Bundled bank verification failed: source=$($bankAssetSources.Count), assets=$copiedBankCount"
}
if ($indexText.Contains("window.__QUIZAPP_EMBEDDED_BANKS__ = null;")) {
  throw "Embedded bank fallback was not written to packaged index.html"
}
$assetAnnouncement = Join-Path $assetDir "distribution\quizapp-announcements.json"
if (-not (Test-Path -LiteralPath $assetAnnouncement)) {
  throw "Bundled announcement feed is missing"
}

$manifestText = [System.IO.File]::ReadAllText((Join-Path $androidDir "AndroidManifest.xml"), [System.Text.Encoding]::UTF8)
$manifestText = [regex]::Replace($manifestText, 'android:versionCode="\d+"', "android:versionCode=`"$VersionCode`"")
$manifestText = [regex]::Replace($manifestText, 'android:versionName="[^"]+"', "android:versionName=`"$resolvedVersionName`"")
[System.IO.File]::WriteAllText($manifestFile, $manifestText, [System.Text.UTF8Encoding]::new($false))

$javaSources = Get-ChildItem -LiteralPath (Join-Path $androidDir "src") -Recurse -Filter "*.java" | ForEach-Object { $_.FullName }
& $javac -encoding UTF-8 -source 8 -target 8 -bootclasspath $platformJar -d $classesDir $javaSources
if ($LASTEXITCODE -ne 0) { throw "javac failed" }

$classFiles = Get-ChildItem -LiteralPath $classesDir -Recurse -Filter "*.class" | ForEach-Object { $_.FullName }
if (-not $classFiles) { throw "No compiled class files found" }
& $d8 --min-api 23 --lib $platformJar --output $dexDir $classFiles
if ($LASTEXITCODE -ne 0) { throw "d8 failed" }

& $aapt2 link `
  -o $unsignedApk `
  --manifest $manifestFile `
  -I $platformJar `
  --min-sdk-version 23 `
  --target-sdk-version 32
if ($LASTEXITCODE -ne 0) { throw "aapt2 link failed" }

Copy-Item -LiteralPath $unsignedApk -Destination $classesApk
Push-Location $dexDir
try {
  & $jar uf $classesApk "classes.dex"
  if ($LASTEXITCODE -ne 0) { throw "jar update failed" }
} finally {
  Pop-Location
}

$assetPackageFiles = Get-ChildItem -LiteralPath $assetDir -Recurse -File | ForEach-Object {
  $_.FullName.Substring($buildDir.Length + 1)
}
Push-Location $buildDir
try {
  & $jar uf $classesApk $assetPackageFiles
  if ($LASTEXITCODE -ne 0) { throw "jar asset update failed" }
} finally {
  Pop-Location
}

& $zipalign -f -p 4 $classesApk $alignedApk
if ($LASTEXITCODE -ne 0) { throw "zipalign failed" }

if (-not (Test-Path -LiteralPath $keystore)) {
  & $keytool -genkeypair -v `
    -keystore $keystore `
    -storepass android `
    -keypass android `
    -alias androiddebugkey `
    -keyalg RSA `
    -keysize 2048 `
    -validity 10000 `
    -dname "CN=Android Debug,O=Android,C=US"
  if ($LASTEXITCODE -ne 0) { throw "keytool failed" }
}

& $apksigner sign `
  --ks $keystore `
  --ks-key-alias androiddebugkey `
  --ks-pass pass:android `
  --key-pass pass:android `
  --out $finalApk `
  $alignedApk
if ($LASTEXITCODE -ne 0) { throw "apksigner failed" }

& $apksigner verify --verbose --print-certs $finalApk
if ($LASTEXITCODE -ne 0) { throw "apksigner verify failed" }

$packagedEntries = @(& $jar tf $finalApk)
$packagedBankCount = @($packagedEntries | Where-Object { $_ -match '^assets/data/.+\.json$' }).Count
if ($packagedBankCount -ne $bankAssetSources.Count) {
  throw "APK bank verification failed: expected=$($bankAssetSources.Count), packaged=$packagedBankCount"
}
if (-not ($packagedEntries -contains "assets/distribution/quizapp-announcements.json")) {
  throw "APK announcement feed verification failed"
}

$apk = Get-Item -LiteralPath $finalApk
Write-Host "APK=$($apk.FullName)"
Write-Host "SIZE=$($apk.Length)"
Write-Host "APP_VERSION=$resolvedAppVersion"
Write-Host "BUILD_COMMIT=$buildCommit"
Write-Host "VERSION_CODE=$VersionCode"
Write-Host "BANK_COUNT=$packagedBankCount"

if ($substCreated) {
  & subst $substDrive /D | Out-Null
}
