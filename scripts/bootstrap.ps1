param([switch]$SkipCef, [switch]$SkipNode)
$ErrorActionPreference = 'Stop'
$taskRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$taskLock = Get-Content -LiteralPath (Join-Path $taskRoot 'dependencies.lock.json') -Raw | ConvertFrom-Json
$taskDownloads = Join-Path $taskRoot 'third_party/downloads'
New-Item -ItemType Directory -Force -Path $taskDownloads | Out-Null
function Get-Dependency([string]$Url, [string]$Name, [string]$ExpectedHash = '', [string]$Algorithm = 'SHA256') {
  $target = Join-Path $taskDownloads $Name
  if (-not (Test-Path -LiteralPath $target)) {
    Write-Host "Downloading $Name"
    Invoke-WebRequest -Uri $Url -OutFile $target
  }
  if ($ExpectedHash -and (Get-FileHash -LiteralPath $target -Algorithm $Algorithm).Hash -ne $ExpectedHash) {
    throw "Checksum mismatch for $Name. Remove the incomplete download and retry."
  }
  return $target
}
if (-not $SkipCef) {
  $cefRoot = Join-Path $taskRoot 'third_party/cef'
  if (-not (Test-Path -LiteralPath (Join-Path $cefRoot 'include/cef_version.h'))) {
    $archive = Get-Dependency ('https://cef-builds.spotifycdn.com/' + $taskLock.cef.archive) $taskLock.cef.archive $taskLock.cef.sha256
    New-Item -ItemType Directory -Force -Path $cefRoot | Out-Null
    & tar.exe -xf $archive -C $cefRoot --strip-components 1
    if ($LASTEXITCODE -ne 0) { throw 'CEF archive extraction failed.' }
  }
  $cefHeader = Get-Content -LiteralPath (Join-Path $cefRoot 'include/cef_version.h') -Raw
  if ($cefHeader -notmatch ('#define CEF_VERSION "' + [Regex]::Escape($taskLock.cef.version) + '"')) { throw 'Existing CEF directory does not match dependencies.lock.json. Move it aside before bootstrapping the updated version.' }
}
$sqliteRoot = Join-Path $taskRoot 'third_party/sqlite'
if (-not (Test-Path -LiteralPath (Join-Path $sqliteRoot 'sqlite3.c'))) {
  $sqliteZip = Get-Dependency $taskLock.sqlite.url 'sqlite.zip' $taskLock.sqlite.sha256
  $sqliteStaging = Join-Path $taskDownloads 'sqlite-extracted'
  Expand-Archive -LiteralPath $sqliteZip -DestinationPath $sqliteStaging -Force
  New-Item -ItemType Directory -Force -Path $sqliteRoot | Out-Null
  $sqliteFiles = Get-ChildItem -LiteralPath $sqliteStaging -Recurse -File | Where-Object Name -in 'sqlite3.c','sqlite3.h','sqlite3ext.h'
  foreach ($file in $sqliteFiles) { Copy-Item -LiteralPath $file.FullName -Destination $sqliteRoot }
}
$sqliteHeader = Get-Content -LiteralPath (Join-Path $sqliteRoot 'sqlite3.h') -Raw
if ($sqliteHeader -notmatch ('#define SQLITE_VERSION\s+"' + [Regex]::Escape($taskLock.sqlite.version) + '"')) { throw 'Existing SQLite directory does not match the dependency lock.' }
$jsonRoot = Join-Path $taskRoot 'third_party/json/nlohmann'
New-Item -ItemType Directory -Force -Path $jsonRoot | Out-Null
$jsonFile = Get-Dependency $taskLock.json.url 'json.hpp' $taskLock.json.sha256
Copy-Item -LiteralPath $jsonFile -Destination (Join-Path $jsonRoot 'json.hpp') -Force
if (-not $SkipNode) {
  $nodeRoot = Join-Path $taskRoot 'third_party/node'
  $nodeVersion = $taskLock.node.version
  if (-not (Test-Path -LiteralPath (Join-Path $nodeRoot 'node.exe'))) {
    $nodeName = "node-v$nodeVersion-win-x64.zip"
    $nodeZip = Get-Dependency "https://nodejs.org/dist/v$nodeVersion/$nodeName" $nodeName $taskLock.node.sha256
    $nodeStaging = Join-Path $taskDownloads 'node-extracted'
    Expand-Archive -LiteralPath $nodeZip -DestinationPath $nodeStaging -Force
    New-Item -ItemType Directory -Force -Path $nodeRoot | Out-Null
    Copy-Item -LiteralPath (Join-Path $nodeStaging "node-v$nodeVersion-win-x64/node.exe") -Destination $nodeRoot
    Copy-Item -LiteralPath (Join-Path $nodeStaging "node-v$nodeVersion-win-x64/LICENSE") -Destination $nodeRoot
  }
  $actualNode = & (Join-Path $nodeRoot 'node.exe') --version
  if ($actualNode -ne "v$nodeVersion") { throw 'Existing bundled Node runtime does not match the dependency lock.' }
}
Write-Host 'Pinned native dependencies are ready.'
