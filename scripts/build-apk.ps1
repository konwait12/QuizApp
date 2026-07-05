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
$finalApk = Join-Path $projectRoot "output\QuizApp-v1.0.10-debug.apk"
$keystore = Join-Path $projectRoot "output\quizapp-debug.keystore"

if (Test-Path -LiteralPath $buildDir) { Remove-Item -LiteralPath $buildDir -Recurse -Force }
New-Item -ItemType Directory -Force -Path $assetDir, $classesDir, $dexDir | Out-Null

Copy-Item -LiteralPath (Join-Path $projectRoot "index.html") -Destination (Join-Path $assetDir "index.html")
$assetDataDir = Join-Path $assetDir "data"
New-Item -ItemType Directory -Force -Path $assetDataDir | Out-Null
$assetBankFiles = @()
$sourceBankFiles = Get-ChildItem -LiteralPath (Join-Path $projectRoot "data") -Filter "*.json" | Sort-Object Name
for ($i = 0; $i -lt $sourceBankFiles.Count; $i++) {
  $assetName = "bank-{0:D3}.json" -f ($i + 1)
  Copy-Item -LiteralPath $sourceBankFiles[$i].FullName -Destination (Join-Path $assetDataDir $assetName)
  $assetBankFiles += $assetName
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
[System.IO.File]::WriteAllText($assetIndex, $indexText, [System.Text.UTF8Encoding]::new($false))

$javaSources = Get-ChildItem -LiteralPath (Join-Path $androidDir "src") -Recurse -Filter "*.java" | ForEach-Object { $_.FullName }
& $javac -encoding UTF-8 -source 8 -target 8 -bootclasspath $platformJar -d $classesDir $javaSources
if ($LASTEXITCODE -ne 0) { throw "javac failed" }

$classFiles = Get-ChildItem -LiteralPath $classesDir -Recurse -Filter "*.class" | ForEach-Object { $_.FullName }
if (-not $classFiles) { throw "No compiled class files found" }
& $d8 --min-api 23 --lib $platformJar --output $dexDir $classFiles
if ($LASTEXITCODE -ne 0) { throw "d8 failed" }

& $aapt2 link `
  -o $unsignedApk `
  --manifest (Join-Path $androidDir "AndroidManifest.xml") `
  -I $platformJar `
  -A $assetDir `
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

$apk = Get-Item -LiteralPath $finalApk
Write-Host "APK=$($apk.FullName)"
Write-Host "SIZE=$($apk.Length)"

if ($substCreated) {
  & subst $substDrive /D | Out-Null
}
