# Turn a completed, audited engineSourceBuild distribution into the archive
# that cef.engineArchive pins. This neither uploads nor edits the lock: a
# maintainer reviews the printed pin, publishes the asset and commits the pin.
param(
  [Parameter(Mandatory=$true)][string]$Distribution,
  [string]$OutputDirectory = (Join-Path $PSScriptRoot '../dist/cef-engine')
)
$ErrorActionPreference = 'Stop'
$taskRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$taskLock = Get-Content -LiteralPath (Join-Path $taskRoot 'dependencies.lock.json') -Raw | ConvertFrom-Json
. (Join-Path $PSScriptRoot 'dependency-extract.ps1')
. (Join-Path $PSScriptRoot 'cef-engine-provenance.ps1')
$taskDistribution = [IO.Path]::GetFullPath($Distribution)
$taskEngine = Get-XenonEngineProvenance -Directory $taskDistribution -Engine (Join-Path $taskDistribution 'Release/libcef.dll') -Lock $taskLock -Distribution
# Only the codec-free engine recipe can become a release input; a codec or
# stock distribution is refused here and again by bootstrap.
if ($taskEngine.Kind -ne 'engine') { throw 'Only an audited engineSourceBuild distribution (with xenon-engine-build.json) can become the release engine archive.' }
if ((Get-Content -LiteralPath (Join-Path $taskDistribution 'include/cef_version.h') -Raw) -notmatch ('#define CEF_VERSION "' + [Regex]::Escape($taskLock.cef.version) + '"')) {
  throw 'The distribution does not match the pinned CEF version.'
}
foreach ($taskRequired in @('include','cmake','libcef_dll','Release','Resources')) {
  if (-not (Test-Path -LiteralPath (Join-Path $taskDistribution $taskRequired) -PathType Container)) { throw "Incomplete CEF distribution: $taskRequired" }
}
foreach ($taskRequired in @('Release/libcef.lib','CMakeLists.txt')) {
  if (-not (Test-Path -LiteralPath (Join-Path $taskDistribution $taskRequired) -PathType Leaf)) { throw "Incomplete CEF distribution: $taskRequired" }
}
$taskSourceFiles = @(Get-ChildItem -LiteralPath $taskDistribution -Recurse -Force)
if (@($taskSourceFiles | Where-Object { $_.Attributes -band [IO.FileAttributes]::ReparsePoint }).Count) { throw 'The distribution must not contain links or junctions.' }
$taskNames = Get-XenonEngineArchiveName -Lock $taskLock -LibcefSha256 $taskEngine.LibcefSha256
$taskOutput = [IO.Path]::GetFullPath($OutputDirectory)
if (($taskOutput + '\').StartsWith($taskDistribution + '\', [StringComparison]::OrdinalIgnoreCase)) { throw 'Write the archive outside the distribution.' }
New-Item -ItemType Directory -Force -Path $taskOutput | Out-Null
$taskArchive = Join-Path $taskOutput $taskNames.Name
if (Test-Path -LiteralPath $taskArchive) { throw "Engine archive already exists: $taskArchive. Never replace a published engine archive's bytes." }
$taskTar = Join-Path ([Environment]::SystemDirectory) 'tar.exe'
$taskSevenZip = Join-Path $env:ProgramFiles '7-Zip/7z.exe'
if (-not (Test-Path -LiteralPath $taskSevenZip -PathType Leaf)) { throw 'Install 7-Zip in Program Files/7-Zip before packaging the engine.' }
$taskStage = Join-Path $taskOutput ('.stage-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $taskStage | Out-Null
try {
  # Same layout as the stock archive: one top-level directory that bootstrap strips.
  $taskTarFile = Join-Path $taskStage ([IO.Path]::GetFileNameWithoutExtension($taskNames.Name))
  & $taskTar -cf $taskTarFile -C (Split-Path -Parent $taskDistribution) (Split-Path -Leaf $taskDistribution)
  if ($LASTEXITCODE -ne 0) { throw 'Creating the engine tar archive failed.' }
  $taskStaged = Join-Path $taskStage $taskNames.Name
  & $taskSevenZip a -tbzip2 -mx=9 -bd -bsp0 $taskStaged $taskTarFile
  if ($LASTEXITCODE -ne 0) { throw 'Compressing the engine archive failed.' }
  Remove-Item -LiteralPath $taskTarFile
  if ((Get-Item -LiteralPath $taskStaged).Length -gt 2147483648) { throw 'The engine archive exceeds the 2 GiB release asset limit.' }
  # Round-trip through bootstrap's own extractor and checks before publishing.
  $taskCheck = Join-Path $taskStage 'extracted'
  Expand-XenonCefArchive -Archive $taskStaged -Destination $taskCheck
  $taskChecked = Get-XenonEngineProvenance -Directory $taskCheck -Engine (Join-Path $taskCheck 'Release/libcef.dll') -Lock $taskLock -Distribution
  if ($taskChecked.Kind -ne 'engine' -or $taskChecked.LibcefSha256 -cne $taskEngine.LibcefSha256) { throw 'The extracted engine archive does not match its distribution.' }
  $taskInventory = { param($Base) @(Get-ChildItem -LiteralPath $Base -Recurse -File -Force | ForEach-Object { $_.FullName.Substring($Base.Length + 1) + '|' + $_.Length } | Sort-Object) -join "`n" }
  if ((& $taskInventory $taskDistribution) -cne (& $taskInventory $taskCheck)) { throw 'The extracted engine archive does not contain exactly the distribution files.' }
  Move-Item -LiteralPath $taskStaged -Destination $taskArchive
} finally {
  # Only this run's uniquely named staging directory is removed.
  if (Test-Path -LiteralPath $taskStage) { Remove-Item -LiteralPath $taskStage -Recurse -Force }
}
$taskDigest = (Get-FileHash -LiteralPath $taskArchive -Algorithm SHA256).Hash.ToLowerInvariant()
$taskPin = [ordered]@{ url=$taskNames.Url; bytes=(Get-Item -LiteralPath $taskArchive).Length; sha256=$taskDigest; libcefSha256=$taskEngine.LibcefSha256 }
$taskPinText = $taskPin | ConvertTo-Json
[IO.File]::WriteAllText(($taskArchive + '.sha256'), "$taskDigest  $($taskNames.Name)`n", [Text.UTF8Encoding]::new($false))
[IO.File]::WriteAllText(($taskArchive + '.pin.json'), $taskPinText + "`n", [Text.UTF8Encoding]::new($false))
Write-Host "Engine archive: $taskArchive"
Write-Host "Publish it unchanged as the only asset of a GitHub prerelease tagged $($taskNames.Tag) in Xero-05/xenon-browser."
Write-Host "Confirm GitHub reports its digest as sha256:$taskDigest, then set cef.engineArchive in dependencies.lock.json to:"
Write-Output $taskPinText
