# When cef.engineArchive pins a Xenon engine build, bootstrap fetches it into
# third_party/cef-engine instead of the stock CEF archive; -StockCef fetches the stock archive.
param([switch]$SkipCef, [switch]$SkipNode, [switch]$DownloadOnly, [switch]$StockCef)
$ErrorActionPreference = 'Stop'
$taskRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$taskLock = Get-Content -LiteralPath (Join-Path $taskRoot 'dependencies.lock.json') -Raw | ConvertFrom-Json
$taskDownloads = Join-Path $taskRoot 'third_party/downloads'
New-Item -ItemType Directory -Force -Path $taskDownloads | Out-Null
. (Join-Path $PSScriptRoot 'dependency-download.ps1')
. (Join-Path $PSScriptRoot 'dependency-extract.ps1')
. (Join-Path $PSScriptRoot 'cef-engine-provenance.ps1')
$taskEnginePin = Get-XenonEngineArchivePin -Lock $taskLock
$taskUseEngine = -not $SkipCef -and $taskEnginePin -and -not $StockCef
$taskUseStock = -not $SkipCef -and -not $taskUseEngine
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
$taskCefArchive = if ($taskUseStock) { Get-Dependency ('https://cef-builds.spotifycdn.com/' + $taskLock.cef.archive) $taskLock.cef.archive $taskLock.cef.sha256 }
# The engine archive is a release asset with a mandatory size and SHA-256 pin.
$taskEngineArchive = if ($taskUseEngine) {
  Get-XenonDependency -Url $taskEnginePin.Url -Name $taskEnginePin.Name -ExpectedHash $taskEnginePin.Sha256 -ExpectedBytes $taskEnginePin.Bytes -DownloadDirectory $taskDownloads
}
$sqliteZip = Get-Dependency $taskLock.sqlite.url 'sqlite.zip' $taskLock.sqlite.sha256
$jsonFile = Get-Dependency $taskLock.json.url 'json.hpp' $taskLock.json.sha256
$nodeVersion = $taskLock.node.version
$nodeName = "node-v$nodeVersion-win-x64.zip"
$nodeZip = if (-not $SkipNode) { Get-Dependency "https://nodejs.org/dist/v$nodeVersion/$nodeName" $nodeName $taskLock.node.sha256 }
if ($DownloadOnly) {
  Write-Host "Pinned downloads are verified and ready to cache ($([Math]::Round($taskBootstrapClock.Elapsed.TotalSeconds,1))s total)."
  return
}
if ($taskUseStock) {
  $cefRoot = Join-Path $taskRoot 'third_party/cef'
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
    Expand-XenonCefArchive -Archive $taskCefArchive -Destination $cefRoot
  }
  $cefHeader = Get-Content -LiteralPath (Join-Path $cefRoot 'include/cef_version.h') -Raw
  if ($cefHeader -notmatch ('#define CEF_VERSION "' + [Regex]::Escape($taskLock.cef.version) + '"')) { throw 'Existing CEF directory does not match dependencies.lock.json. Move it aside before bootstrapping the updated version.' }
  Write-Host "[ready] CEF $($taskLock.cef.version)"
}
if ($taskUseEngine) {
  # A separate directory leaves any stock distribution in third_party/cef intact.
  $engineRoot = Join-Path $taskRoot 'third_party/cef-engine'
  $engineHeaderPath = Join-Path $engineRoot 'include/cef_version.h'
  if (Test-Path -LiteralPath $engineHeaderPath) {
    $engineExistingVersion = [Regex]::Match((Get-Content -LiteralPath $engineHeaderPath -Raw), '#define CEF_VERSION "([^"]+)"')
    if ($engineExistingVersion.Success -and $engineExistingVersion.Groups[1].Value -ne $taskLock.cef.version) { throw 'Existing CEF engine directory does not match dependencies.lock.json. Move it aside before bootstrapping the updated version.' }
  }
  Invoke-VerifiedExtraction 'Xenon CEF engine' $taskEnginePin.Sha256 (Join-Path $engineRoot '.xenon-extracted-sha256') @(
    $engineHeaderPath, (Join-Path $engineRoot 'Release/libcef.dll'), (Join-Path $engineRoot 'Release/bootstrap.exe'),
    (Join-Path $engineRoot 'LICENSE.txt'), (Join-Path $engineRoot 'CREDITS.html'), (Join-Path $engineRoot 'xenon-engine-build.json')) {
    Expand-XenonCefArchive -Archive $taskEngineArchive -Destination $engineRoot
  }
  if ((Get-Content -LiteralPath $engineHeaderPath -Raw) -notmatch ('#define CEF_VERSION "' + [Regex]::Escape($taskLock.cef.version) + '"')) { throw 'The pinned CEF engine archive does not match dependencies.lock.json.' }
  # Recheck on every run: the archive's manifest must match the pinned recipe,
  # every file it lists and the pinned libcef.dll hash.
  [void](Get-XenonEngineProvenance -Directory $engineRoot -Engine (Join-Path $engineRoot 'Release/libcef.dll') -Lock $taskLock -Distribution -Release)
  Write-Host "[ready] Xenon CEF engine $($taskEnginePin.Tag)"
}
$sqliteRoot = Join-Path $taskRoot 'third_party/sqlite'
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
Copy-Item -LiteralPath $jsonFile -Destination (Join-Path $jsonRoot 'json.hpp') -Force
Write-Host "[ready] nlohmann/json $($taskLock.json.version)"
if (-not $SkipNode) {
  $nodeRoot = Join-Path $taskRoot 'third_party/node'
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
