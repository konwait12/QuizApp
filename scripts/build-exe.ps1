param(
  [string]$Version = "v1.0.16"
)

$ErrorActionPreference = "Stop"

$projectRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$desktopProject = Join-Path $projectRoot "desktop\QuizAppDesktop\QuizAppDesktop.csproj"
$buildDir = Join-Path $projectRoot "output\desktop-build"
$finalExe = Join-Path $projectRoot "output\QuizApp-$Version-win-x64.exe"

if (Test-Path -LiteralPath $buildDir) {
  Remove-Item -LiteralPath $buildDir -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $buildDir | Out-Null

dotnet publish $desktopProject `
  -c Release `
  -r win-x64 `
  --self-contained true `
  -p:PublishSingleFile=true `
  -p:EnableCompressionInSingleFile=true `
  -p:PublishTrimmed=false `
  -o $buildDir
if ($LASTEXITCODE -ne 0) { throw "dotnet publish failed" }

$publishedExe = Join-Path $buildDir "QuizApp.exe"
if (-not (Test-Path -LiteralPath $publishedExe)) {
  throw "Published exe not found: $publishedExe"
}

Copy-Item -LiteralPath $publishedExe -Destination $finalExe -Force
$exe = Get-Item -LiteralPath $finalExe
Write-Host "EXE=$($exe.FullName)"
Write-Host "SIZE=$($exe.Length)"
Write-Host "PORT=0721"
