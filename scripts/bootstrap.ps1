param([switch]$SkipCef, [switch]$SkipNode)
$ErrorActionPreference = 'Stop'
$taskRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$taskLock = Get-Content -LiteralPath (Join-Path $taskRoot 'dependencies.lock.json') -Raw | ConvertFrom-Json
$taskDownloads = Join-Path $taskRoot 'third_party/downloads'
New-Item -ItemType Directory -Force -Path $taskDownloads | Out-Null
. (Join-Path $PSScriptRoot 'dependency-download.ps1')
. (Join-Path $PSScriptRoot 'dependency-extract.ps1')
function Get-Dependency([string]$Url, [string]$Name, [string]$ExpectedHash) {
  Get-XenonDependency -Url $Url -Name $Name -ExpectedHash $ExpectedHash -DownloadDirectory $taskDownloads
}
function Invoke-VerifiedExtraction([string]$Name, [string]$Hash, [string]$Marker, [string[]]$RequiredFiles, [scriptblock]$Extract) {
  $taskComplete = (Test-Path -LiteralPath $Marker -PathType Leaf) -and
    ((Get-Content -LiteralPath $Marker -Raw).Trim() -ieq $Hash)
  foreach ($taskRequired in $RequiredFiles) {
    if (-not (Test-Path -LiteralPath $taskRequired -PathType Leaf)) { $taskComplete = $false }
  }
  if ($taskComplete) { return }
  # A marker is written only after successful extraction. A process interrupted
  # after one recognizable file appears must not be mistaken for completion.
  if (Test-Path -LiteralPath $Marker) { Remove-Item -LiteralPath $Marker }
  Write-Host "[extract] Extracting verified $Name archive"
  $taskExtractClock = [Diagnostics.Stopwatch]::StartNew()
  & $Extract
  foreach ($taskRequired in $RequiredFiles) {
    if (-not (Test-Path -LiteralPath $taskRequired -PathType Leaf)) { throw "$Name extraction did not produce all required files." }
  }
  [IO.File]::WriteAllText($Marker, $Hash)
  Write-Host "[extract] $Name completed in $([Math]::Round($taskExtractClock.Elapsed.TotalSeconds,1))s"
}
$taskBootstrapClock = [Diagnostics.Stopwatch]::StartNew()
if (-not $SkipCef) {
  $cefRoot = Join-Path $taskRoot 'third_party/cef'
  $archive = Get-Dependency ('https://cef-builds.spotifycdn.com/' + $taskLock.cef.archive) $taskLock.cef.archive $taskLock.cef.sha256
  if (Test-Path -LiteralPath (Join-Path $cefRoot 'include/cef_version.h')) {
    $cefHeader = Get-Content -LiteralPath (Join-Path $cefRoot 'include/cef_version.h') -Raw
    # A recognizable different installation still needs an explicit move aside.
    # A truncated header without a completed extraction marker is repairable.
    $taskExistingCefVersion = [Regex]::Match($cefHeader, '#define CEF_VERSION "([^"]+)"')
    if ($taskExistingCefVersion.Success -and $taskExistingCefVersion.Groups[1].Value -ne $taskLock.cef.version) { throw 'Existing CEF directory does not match dependencies.lock.json. Move it aside before bootstrapping the updated version.' }
  }
  Invoke-VerifiedExtraction 'CEF' $taskLock.cef.sha256 (Join-Path $cefRoot '.xenon-extracted-sha256') @(
    (Join-Path $cefRoot 'include/cef_version.h'), (Join-Path $cefRoot 'Release/libcef.dll'),
    (Join-Path $cefRoot 'Release/bootstrap.exe'), (Join-Path $cefRoot 'LICENSE.txt'), (Join-Path $cefRoot 'CREDITS.html')) {
    Expand-XenonCefArchive -Archive $archive -Destination $cefRoot
  }
  $cefHeader = Get-Content -LiteralPath (Join-Path $cefRoot 'include/cef_version.h') -Raw
  if ($cefHeader -notmatch ('#define CEF_VERSION "' + [Regex]::Escape($taskLock.cef.version) + '"')) { throw 'Existing CEF directory does not match dependencies.lock.json. Move it aside before bootstrapping the updated version.' }
  Write-Host "[ready] CEF $($taskLock.cef.version)"
}
$sqliteRoot = Join-Path $taskRoot 'third_party/sqlite'
$sqliteZip = Get-Dependency $taskLock.sqlite.url 'sqlite.zip' $taskLock.sqlite.sha256
Invoke-VerifiedExtraction 'SQLite' $taskLock.sqlite.sha256 (Join-Path $sqliteRoot '.xenon-extracted-sha256') @(
  (Join-Path $sqliteRoot 'sqlite3.c'),(Join-Path $sqliteRoot 'sqlite3.h'),(Join-Path $sqliteRoot 'sqlite3ext.h')) {
  $sqliteStaging = Join-Path $taskDownloads 'sqlite-extracted'
  Expand-Archive -LiteralPath $sqliteZip -DestinationPath $sqliteStaging -Force
  New-Item -ItemType Directory -Force -Path $sqliteRoot | Out-Null
  $sqliteFiles = Get-ChildItem -LiteralPath $sqliteStaging -Recurse -File | Where-Object Name -in 'sqlite3.c','sqlite3.h','sqlite3ext.h'
  foreach ($file in $sqliteFiles) { Copy-Item -LiteralPath $file.FullName -Destination $sqliteRoot }
}
$sqliteHeader = Get-Content -LiteralPath (Join-Path $sqliteRoot 'sqlite3.h') -Raw
if ($sqliteHeader -notmatch ('#define SQLITE_VERSION\s+"' + [Regex]::Escape($taskLock.sqlite.version) + '"')) { throw 'Existing SQLite directory does not match the dependency lock.' }
Write-Host "[ready] SQLite $($taskLock.sqlite.version)"
$jsonRoot = Join-Path $taskRoot 'third_party/json/nlohmann'
New-Item -ItemType Directory -Force -Path $jsonRoot | Out-Null
$jsonFile = Get-Dependency $taskLock.json.url 'json.hpp' $taskLock.json.sha256
Copy-Item -LiteralPath $jsonFile -Destination (Join-Path $jsonRoot 'json.hpp') -Force
Write-Host "[ready] nlohmann/json $($taskLock.json.version)"
if (-not $SkipNode) {
  $nodeRoot = Join-Path $taskRoot 'third_party/node'
  $nodeVersion = $taskLock.node.version
  $nodeName = "node-v$nodeVersion-win-x64.zip"
  $nodeZip = Get-Dependency "https://nodejs.org/dist/v$nodeVersion/$nodeName" $nodeName $taskLock.node.sha256
  $nodeStaging = Join-Path $taskDownloads 'node-extracted'
  # The distribution also supplies npm to CI; a cached archive is not an
  # extracted Node installation. Never cache the extracted directory itself.
  $nodeDistribution = Join-Path $nodeStaging "node-v$nodeVersion-win-x64"
  Invoke-VerifiedExtraction 'Node' $taskLock.node.sha256 (Join-Path $nodeDistribution '.xenon-extracted-sha256') @(
    (Join-Path $nodeDistribution 'npm.cmd'), (Join-Path $nodeDistribution 'node_modules/npm/bin/npm-cli.js'),
    (Join-Path $nodeDistribution 'node.exe'), (Join-Path $nodeDistribution 'LICENSE')) {
    Expand-Archive -LiteralPath $nodeZip -DestinationPath $nodeStaging -Force
  }
  if (-not (Test-Path -LiteralPath (Join-Path $nodeRoot 'node.exe'))) {
    New-Item -ItemType Directory -Force -Path $nodeRoot | Out-Null
    Copy-Item -LiteralPath (Join-Path $nodeStaging "node-v$nodeVersion-win-x64/node.exe") -Destination $nodeRoot
  }
  Copy-Item -LiteralPath (Join-Path $nodeDistribution 'LICENSE') -Destination $nodeRoot -Force
  $actualNode = & (Join-Path $nodeRoot 'node.exe') --version
  if ($actualNode -ne "v$nodeVersion") { throw 'Existing bundled Node runtime does not match the dependency lock.' }
  Write-Host "[ready] Node $nodeVersion"
}
Write-Host "Pinned native dependencies are ready ($([Math]::Round($taskBootstrapClock.Elapsed.TotalSeconds,1))s total)."
