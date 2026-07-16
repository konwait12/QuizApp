param(
    [string]$Ref = 'dd5386366b4b1a51a6e960491feb82e777fbdcb2',
    [switch]$Force
)

$ErrorActionPreference = 'Stop'

$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$vendorRoot = [IO.Path]::GetFullPath((Join-Path $projectRoot 'native\third_party\speedynote'))
$destination = [IO.Path]::GetFullPath((Join-Path $vendorRoot 'upstream'))
$cacheRoot = [IO.Path]::GetFullPath((Join-Path $projectRoot 'output\cache\speedynote'))

if (-not $destination.StartsWith($vendorRoot, [StringComparison]::OrdinalIgnoreCase)) {
    throw "Refusing to write outside the SpeedyNote vendor directory: $destination"
}

if (Test-Path -LiteralPath $destination) {
    $existing = Get-ChildItem -LiteralPath $destination -Force -ErrorAction SilentlyContinue
    if ($existing -and -not $Force) {
        throw 'SpeedyNote source already exists. Use -Force only when intentionally replacing the pinned source.'
    }
    if ($Force) {
        Remove-Item -LiteralPath $destination -Recurse -Force
    }
}

New-Item -ItemType Directory -Path $cacheRoot -Force | Out-Null
$archive = Join-Path $cacheRoot "$Ref.zip"
$extractRoot = Join-Path $cacheRoot "extract-$Ref"

if (-not (Test-Path -LiteralPath $archive)) {
    Invoke-WebRequest -Uri "https://github.com/alpha-liu-01/SpeedyNote/archive/$Ref.zip" -OutFile $archive
}
if (Test-Path -LiteralPath $extractRoot) {
    Remove-Item -LiteralPath $extractRoot -Recurse -Force
}
Expand-Archive -LiteralPath $archive -DestinationPath $extractRoot -Force

$sourceRoot = Get-ChildItem -LiteralPath $extractRoot -Directory | Select-Object -First 1
if (-not $sourceRoot -or -not (Test-Path -LiteralPath (Join-Path $sourceRoot.FullName 'CMakeLists.txt'))) {
    throw 'Downloaded archive does not contain the expected SpeedyNote source tree.'
}

New-Item -ItemType Directory -Path $destination -Force | Out-Null
Get-ChildItem -LiteralPath $sourceRoot.FullName -Force |
    Copy-Item -Destination $destination -Recurse -Force

$metadata = [ordered]@{
    repository = 'https://github.com/alpha-liu-01/SpeedyNote'
    commit = $Ref
    fetchedAt = [DateTime]::UtcNow.ToString('o')
}
$json = $metadata | ConvertTo-Json
[IO.File]::WriteAllText(
    (Join-Path $destination '.quizapp-upstream.json'),
    $json,
    [Text.UTF8Encoding]::new($false)
)

Write-Host "SpeedyNote $Ref extracted to $destination"
