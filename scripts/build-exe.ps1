param(
    [string]$Version = "v1.0.20",
    [string]$BuildCommit = ""
)

$ErrorActionPreference = "Stop"

$projectRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$desktopProject = Join-Path $projectRoot "desktop\QuizAppDesktop\QuizAppDesktop.csproj"
$buildDir = Join-Path $projectRoot "output\desktop-build"
$finalExe = Join-Path $projectRoot "output\QuizApp-$Version-win-x64.exe"
$generatedIndex = Join-Path $buildDir "index.html"

if (Test-Path -LiteralPath $buildDir) {
  Remove-Item -LiteralPath $buildDir -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $buildDir | Out-Null

$resolvedBuildCommit = if ($BuildCommit) { $BuildCommit.Trim() } else { "dev" }
if (-not $BuildCommit) {
  try {
    $gitCommit = (& git -C $projectRoot rev-parse HEAD 2>$null)
    if ($LASTEXITCODE -eq 0 -and $gitCommit) {
      $resolvedBuildCommit = ($gitCommit | Select-Object -First 1).Trim()
    }
  } catch {
    $resolvedBuildCommit = "dev"
  }
}

$indexText = [System.IO.File]::ReadAllText((Join-Path $projectRoot "index.html"), [System.Text.Encoding]::UTF8)
$indexText = [regex]::Replace($indexText, "const APP_VERSION = '[^']+';", "const APP_VERSION = '$Version';")
$indexText = [regex]::Replace($indexText, "const APP_BUILD_COMMIT = '[^']*';", "const APP_BUILD_COMMIT = '$resolvedBuildCommit';")
[System.IO.File]::WriteAllText($generatedIndex, $indexText, [System.Text.UTF8Encoding]::new($false))

dotnet publish $desktopProject `
  -c Release `
  -r win-x64 `
  --self-contained true `
  -p:PublishSingleFile=true `
  -p:EnableCompressionInSingleFile=true `
  -p:PublishTrimmed=false `
  "-p:QuizAppIndex=$generatedIndex" `
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
Write-Host "APP_VERSION=$Version"
Write-Host "BUILD_COMMIT=$resolvedBuildCommit"
