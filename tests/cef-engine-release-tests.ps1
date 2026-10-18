# Fail-closed checks for the release engine path: provenance acceptance, the
# engine archive pin and a real tar/7-Zip round trip of the archive builder.
# Uses synthetic distributions only; no CEF build, download or upload.
$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path $PSScriptRoot -Parent
$taskLock = Get-Content (Join-Path $taskRoot 'dependencies.lock.json') -Raw | ConvertFrom-Json
. (Join-Path $taskRoot 'scripts/dependency-extract.ps1')
. (Join-Path $taskRoot 'scripts/cef-engine-provenance.ps1')
# Keep synthetic evidence in the ignored workspace; never remove user directories.
$taskSuite = Join-Path $taskRoot ('.cache/cef-engine-release-tests-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $taskSuite | Out-Null
$taskChecks = 0

function Copy-TaskLock($Lock) { $Lock | ConvertTo-Json -Depth 20 | ConvertFrom-Json }
function Get-TaskHash([string]$Path) { (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant() }
function Write-TaskFile([string]$Path, $Content) {
  New-Item -ItemType Directory -Force -Path (Split-Path -Parent $Path) | Out-Null
  if ($Content -is [byte[]]) { [IO.File]::WriteAllBytes($Path, $Content) } else { [IO.File]::WriteAllText($Path, $Content) }
}
function New-TaskDistribution([string]$Name, [string]$Kind = 'engine', [scriptblock]$Edit) {
  $taskPath = Join-Path $taskSuite $Name
  Write-TaskFile (Join-Path $taskPath 'include/cef_version.h') "#define CEF_VERSION `"$($taskLock.cef.version)`"`n"
  Write-TaskFile (Join-Path $taskPath 'CMakeLists.txt') "# synthetic fixture`n"
  Write-TaskFile (Join-Path $taskPath 'cmake/cef_variables.cmake') "# synthetic fixture`n"
  Write-TaskFile (Join-Path $taskPath 'libcef_dll/wrapper.cc') "// synthetic fixture`n"
  # Unique engine bytes per fixture, large enough to exercise compression.
  $taskBytes = [byte[]]::new(256KB); [Security.Cryptography.RandomNumberGenerator]::Create().GetBytes($taskBytes)
  Write-TaskFile (Join-Path $taskPath 'Release/libcef.dll') $taskBytes
  foreach ($taskFile in 'Release/libcef.lib','Release/bootstrap.exe','Resources/resources.pak','LICENSE.txt','CREDITS.html') {
    Write-TaskFile (Join-Path $taskPath $taskFile) "synthetic fixture: $taskFile`n"
  }
  if ($Kind -eq 'stock') { return $taskPath }
  $taskFiles = [ordered]@{}
  foreach ($taskFile in 'Release/libcef.dll','Release/bootstrap.exe','LICENSE.txt','CREDITS.html') { $taskFiles[$taskFile] = Get-TaskHash (Join-Path $taskPath $taskFile) }
  if ($Kind -eq 'engine') {
    $taskManifest = [ordered]@{ recipeName='engineSourceBuild'; proprietaryCodecs=$false
      effectiveArgs=[ordered]@{ proprietary_codecs='false'; ffmpeg_branding='"Chromium"' }; codecBackend='none'
      recipe=$taskLock.cef.engineSourceBuild; bundledH264AacDecoders=$false; bundledOpenH264=$false; files=$taskFiles; limitations='synthetic fixture' }
    $taskManifestName = 'xenon-engine-build.json'
  } else {
    $taskManifest = [ordered]@{ codecBackend='windows'; recipe=$taskLock.cef.mediaSourceBuild
      bundledH264AacDecoders=$false; bundledOpenH264=$false; files=$taskFiles; limitations='synthetic fixture' }
    $taskManifestName = 'xenon-codec-build.json'
  }
  if ($Edit) { & $Edit $taskManifest }
  Write-TaskFile (Join-Path $taskPath $taskManifestName) ($taskManifest | ConvertTo-Json -Depth 10)
  $taskPath
}
function Get-TaskEngine([string]$Path, $Lock = $taskLock, [switch]$Release, [switch]$Distribution) {
  Get-XenonEngineProvenance -Directory $Path -Engine (Join-Path $Path 'Release/libcef.dll') -Lock $Lock -Release:$Release -Distribution:$Distribution
}
function New-TaskPinnedLock([string]$LibcefSha256, $Lock = $taskLock) {
  $taskPinned = Copy-TaskLock $Lock
  $taskPinned.cef | Add-Member -NotePropertyName engineArchive -NotePropertyValue ([pscustomobject][ordered]@{
    url=(Get-XenonEngineArchiveName -Lock $taskPinned -LibcefSha256 $LibcefSha256).Url; bytes=123456; sha256=('ab' * 32); libcefSha256=$LibcefSha256 })
  $taskPinned
}
function Assert-TaskAccepted([string]$Name, [scriptblock]$Check) {
  try { $taskResult = & $Check } catch { throw "Unexpectedly rejected ${Name}: $_" }
  $script:taskChecks++
  $taskResult
}
function Assert-TaskRejected([string]$Name, [string]$Pattern, [scriptblock]$Check) {
  $taskFailure = $null
  try { & $Check | Out-Null } catch { $taskFailure = $_ }
  if (-not $taskFailure) { throw "Unexpectedly accepted $Name." }
  if ("$taskFailure" -notmatch $Pattern) { throw "Rejected $Name for the wrong reason: $taskFailure" }
  $script:taskChecks++
}

if ($taskLock.cef.engineArchive) { throw 'This suite expects the lock to keep the stock release engine until an archive is pinned; update its stock-path cases.' }

# Provenance acceptance in build, package and bootstrap.
$taskStock = New-TaskDistribution 'stock' 'stock'
$taskEngineDir = New-TaskDistribution 'engine'
$taskEngineHash = Get-TaskHash (Join-Path $taskEngineDir 'Release/libcef.dll')
$taskCodecDir = New-TaskDistribution 'codec' 'codec'
$taskPinned = New-TaskPinnedLock $taskEngineHash
$taskResult = Assert-TaskAccepted 'stock engine' { Get-TaskEngine $taskStock -Release }
if ($taskResult.Kind -ne 'stock' -or $taskResult.Manifest) { throw 'Stock engine misidentified.' }
Assert-TaskRejected 'stock engine once an archive is pinned' 'pinned in cef.engineArchive' { Get-TaskEngine $taskStock $taskPinned -Release }
$taskResult = Assert-TaskAccepted 'engine distribution' { Get-TaskEngine $taskEngineDir -Distribution }
if ($taskResult.Kind -ne 'engine' -or $taskResult.LibcefSha256 -cne $taskEngineHash) { throw 'Engine misidentified.' }
Assert-TaskRejected 'unpinned engine for a release' 'stock CEF engine until' { Get-TaskEngine $taskEngineDir -Release }
[void](Assert-TaskAccepted 'pinned engine for a release' { Get-TaskEngine $taskEngineDir $taskPinned -Release -Distribution })
Assert-TaskRejected 'an engine other than the pinned one' 'pinned in cef.engineArchive' {
  Get-TaskEngine $taskEngineDir (New-TaskPinnedLock ('0' * 64)) -Release }
$taskOtherRecipe = Copy-TaskLock $taskLock
$taskOtherRecipe.cef.engineSourceBuild.backgroundFrameRatePatch.sha256 = '0' * 64
Assert-TaskRejected 'an engine built from another recipe' 'pinned source build recipe' { Get-TaskEngine $taskEngineDir $taskOtherRecipe }
$taskWithoutRecipe = Copy-TaskLock $taskLock
$taskWithoutRecipe.cef.PSObject.Properties.Remove('engineSourceBuild')
Assert-TaskRejected 'an engine without a pinned recipe' 'pinned source build recipe' { Get-TaskEngine $taskEngineDir $taskWithoutRecipe }
$taskCodecRecipe = Copy-TaskLock $taskLock
$taskCodecRecipe.cef.engineSourceBuild.gnDefines = $taskCodecRecipe.cef.engineSourceBuild.gnDefines.Replace('proprietary_codecs=false', 'proprietary_codecs=true')
$taskCodecRecipeDir = New-TaskDistribution 'engine-codec-recipe' 'engine' { param($m) $m.recipe = $taskCodecRecipe.cef.engineSourceBuild }
Assert-TaskRejected 'an engine recipe edited to enable codecs' 'proprietary codecs disabled' { Get-TaskEngine $taskCodecRecipeDir $taskCodecRecipe }
$taskTampered = New-TaskDistribution 'engine-replaced-dll'
Write-TaskFile (Join-Path $taskTampered 'Release/libcef.dll') 'replaced engine'
Assert-TaskRejected 'a replaced libcef.dll' 'does not match libcef.dll' { Get-TaskEngine $taskTampered }
$taskTampered = New-TaskDistribution 'engine-replaced-license'
Write-TaskFile (Join-Path $taskTampered 'LICENSE.txt') 'replaced notices'
[void](Assert-TaskAccepted 'a build output that only binds libcef.dll' { Get-TaskEngine $taskTampered })
Assert-TaskRejected 'a distribution file that differs from provenance' 'does not match its provenance' { Get-TaskEngine $taskTampered -Distribution }
foreach ($taskFlag in 'proprietaryCodecs','bundledH264AacDecoders','bundledOpenH264') {
  $taskClaim = New-TaskDistribution "engine-$taskFlag" 'engine' ([scriptblock]::Create("param(`$m) `$m.$taskFlag = `$true"))
  Assert-TaskRejected "engine provenance with $taskFlag" $taskFlag { Get-TaskEngine $taskClaim }
  $taskMissing = New-TaskDistribution "engine-no-$taskFlag" 'engine' ([scriptblock]::Create("param(`$m) `$m.Remove('$taskFlag')"))
  Assert-TaskRejected "engine provenance without $taskFlag" $taskFlag { Get-TaskEngine $taskMissing }
}
$taskBackend = New-TaskDistribution 'engine-windows-backend' 'engine' { param($m) $m.codecBackend = 'windows' }
Assert-TaskRejected 'engine provenance naming a codec backend' 'describe the engine recipe' { Get-TaskEngine $taskBackend }
$taskTraversal = New-TaskDistribution 'engine-traversal' 'engine' { param($m) $m.files['../outside.txt'] = '0' * 64 }
Assert-TaskRejected 'a provenance path outside the distribution' 'Unexpected provenance file path' { Get-TaskEngine $taskTraversal -Distribution }
$taskBoth = New-TaskDistribution 'both-manifests'
Copy-Item -LiteralPath (Join-Path $taskCodecDir 'xenon-codec-build.json') -Destination $taskBoth
Assert-TaskRejected 'an engine with both manifests' 'both codec and engine' { Get-TaskEngine $taskBoth }
$taskResult = Assert-TaskAccepted 'a local codec engine build' { Get-TaskEngine $taskCodecDir -Distribution }
if ($taskResult.Kind -ne 'codec') { throw 'Codec engine misidentified.' }
# The codec engine stays out of releases whether or not an archive is pinned.
Assert-TaskRejected 'the codec engine for an unpinned release' 'stock CEF engine until' { Get-TaskEngine $taskCodecDir -Release }
Assert-TaskRejected 'the codec engine for a pinned release' 'pinned in cef.engineArchive' {
  Get-TaskEngine $taskCodecDir (New-TaskPinnedLock (Get-TaskHash (Join-Path $taskCodecDir 'Release/libcef.dll'))) -Release }

# The pin names a fixed release asset whose tag the native updater ignores.
$taskPin = Assert-TaskAccepted 'a well-formed engine archive pin' { Get-XenonEngineArchivePin -Lock $taskPinned }
$taskShort = ($taskLock.cef.version -split '\+')[0]
if ($taskPin.Tag -cne "cef-engine-$taskShort-$($taskEngineHash.Substring(0,12))" -or $taskPin.Tag.StartsWith('v') -or
    $taskPin.Url -cne "https://github.com/Xero-05/xenon-browser/releases/download/$($taskPin.Tag)/xenon-$($taskPin.Tag).tar.bz2") {
  throw "Unexpected engine archive location: $($taskPin.Url)"
}
if ($null -ne (Get-XenonEngineArchivePin -Lock $taskLock)) { throw 'An unpinned lock must keep the stock path.' }
$taskPinEdits = [ordered]@{
  'another repository' = @('url', { param($p) $p.url.Replace('Xero-05/xenon-browser', 'example/xenon-browser') }, 'cef.engineArchive.url')
  'plain HTTP' = @('url', { param($p) $p.url.Replace('https://', 'http://') }, 'cef.engineArchive.url')
  'a browser release tag' = @('url', { param($p) $p.url.Replace('/cef-engine-', '/v0.2.0-cef-engine-') }, 'cef.engineArchive.url')
  'an uppercase digest' = @('sha256', { param($p) $p.sha256.ToUpperInvariant() }, 'lowercase SHA-256')
  'a short engine digest' = @('libcefSha256', { param($p) $p.libcefSha256.Substring(1) }, 'lowercase SHA-256')
  'a textual size' = @('bytes', { param($p) '123456' }, 'archive size')
  'a zero size' = @('bytes', { param($p) 0 }, 'archive size')
  'an oversized asset' = @('bytes', { param($p) [long]2147483649 }, 'archive size')
}
foreach ($taskEdit in $taskPinEdits.GetEnumerator()) {
  $taskEdited = Copy-TaskLock $taskPinned
  $taskEdited.cef.engineArchive.($taskEdit.Value[0]) = & $taskEdit.Value[1] $taskEdited.cef.engineArchive
  Assert-TaskRejected "a pin with $($taskEdit.Key)" ([Regex]::Escape($taskEdit.Value[2])) { Get-XenonEngineArchivePin -Lock $taskEdited }
}
$taskEdited = Copy-TaskLock $taskPinned
$taskEdited.cef.engineArchive | Add-Member -NotePropertyName mirror -NotePropertyValue 'https://example.invalid/engine.tar.bz2'
Assert-TaskRejected 'a pin with an extra location' 'exactly url, bytes' { Get-XenonEngineArchivePin -Lock $taskEdited }
$taskEdited = Copy-TaskLock $taskPinned
$taskEdited.cef.engineArchive.PSObject.Properties.Remove('bytes')
Assert-TaskRejected 'a pin without a size' 'exactly url, bytes' { Get-XenonEngineArchivePin -Lock $taskEdited }
$taskEdited = New-TaskPinnedLock $taskEngineHash $taskCodecRecipe
Assert-TaskRejected 'a pin for a codec-enabled recipe' 'proprietary codecs disabled' { Get-XenonEngineArchivePin -Lock $taskEdited }

# The archive builder accepts only the engine recipe, and its archive passes
# bootstrap's own extraction and release checks.
$taskPackager = Join-Path $taskRoot 'scripts/package-cef-engine.ps1'
$taskOutput = Join-Path $taskSuite 'archives'
[void](Assert-TaskAccepted 'packaging the engine distribution' { & $taskPackager -Distribution $taskEngineDir -OutputDirectory $taskOutput 6>$null })
$taskNames = Get-XenonEngineArchiveName -Lock $taskLock -LibcefSha256 $taskEngineHash
$taskArchive = Join-Path $taskOutput $taskNames.Name
$taskPinFile = Get-Content -LiteralPath ($taskArchive + '.pin.json') -Raw | ConvertFrom-Json
if ($taskPinFile.url -cne $taskNames.Url -or $taskPinFile.bytes -ne (Get-Item -LiteralPath $taskArchive).Length -or
    $taskPinFile.sha256 -cne (Get-TaskHash $taskArchive) -or $taskPinFile.libcefSha256 -cne $taskEngineHash) { throw 'The printed pin does not describe the archive.' }
if ((Get-Content -LiteralPath ($taskArchive + '.sha256') -Raw).Trim() -cne "$($taskPinFile.sha256)  $($taskNames.Name)") { throw 'Unexpected checksum file.' }
if (@(Get-ChildItem -LiteralPath $taskOutput -Force -Filter '.stage-*').Count) { throw 'The archive builder left its staging directory.' }
$taskArchivePinned = Copy-TaskLock $taskLock
$taskArchivePinned.cef | Add-Member -NotePropertyName engineArchive -NotePropertyValue $taskPinFile
[void](Assert-TaskAccepted 'the printed pin' { Get-XenonEngineArchivePin -Lock $taskArchivePinned })
$taskExtracted = Join-Path $taskSuite 'bootstrap-engine'
Expand-XenonCefArchive -Archive $taskArchive -Destination $taskExtracted 6>$null
[void](Assert-TaskAccepted 'the extracted archive for a pinned release' { Get-TaskEngine $taskExtracted $taskArchivePinned -Release -Distribution })
Assert-TaskRejected 'repackaging over a published archive name' 'already exists' { & $taskPackager -Distribution $taskEngineDir -OutputDirectory $taskOutput 6>$null }
Assert-TaskRejected 'packaging the codec engine' 'Only an audited engineSourceBuild' { & $taskPackager -Distribution $taskCodecDir -OutputDirectory $taskOutput 6>$null }
Assert-TaskRejected 'packaging the stock engine' 'Only an audited engineSourceBuild' { & $taskPackager -Distribution $taskStock -OutputDirectory $taskOutput 6>$null }
$taskTampered = New-TaskDistribution 'engine-unaudited-notices'
Write-TaskFile (Join-Path $taskTampered 'CREDITS.html') 'replaced credits'
Assert-TaskRejected 'packaging files that differ from provenance' 'does not match its provenance' { & $taskPackager -Distribution $taskTampered -OutputDirectory $taskOutput 6>$null }
if (@(Get-ChildItem -LiteralPath $taskOutput -File -Filter '*.tar.bz2').Count -ne 1) { throw 'A rejected distribution produced an archive.' }
Write-Output "$taskChecks CEF engine release checks passed."
