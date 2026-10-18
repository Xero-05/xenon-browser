# Shared by bootstrap, build, packaging and the engine archive builder.
# A CEF engine is the stock distribution (no Xenon manifest) or a source build
# whose provenance manifest matches its pinned recipe and libcef.dll bytes.
# With -Release (CI), packages may carry only the engine dependencies.lock.json
# names for releases: the stock engine until cef.engineArchive pins a Xenon
# engine build. Locally built codec-enabled releases do not pass -Release.

# The fixed release location. A different host requires a reviewed change here.
$XenonEngineReleaseBase = 'https://github.com/Xero-05/xenon-browser/releases/download/'

function Get-XenonEngineArchiveName {
  param([Parameter(Mandatory=$true)]$Lock, [Parameter(Mandatory=$true)][string]$LibcefSha256)
  if ($LibcefSha256 -cnotmatch '^[0-9a-f]{64}$') { throw 'The engine archive name needs a lowercase libcef.dll SHA-256.' }
  $taskCefShort = ($Lock.cef.version -split '\+')[0]
  if ($taskCefShort -cnotmatch '^[0-9]+\.[0-9]+\.[0-9]+$') { throw 'Unexpected pinned CEF version.' }
  # The tag never starts with "v", so the native updater never treats it as a browser release.
  $taskTag = "cef-engine-$taskCefShort-$($LibcefSha256.Substring(0,12))"
  $taskName = "xenon-$taskTag.tar.bz2"
  [pscustomobject]@{ Tag=$taskTag; Name=$taskName; Url=($XenonEngineReleaseBase + $taskTag + '/' + $taskName) }
}

function Assert-XenonEngineRecipe($Recipe) {
  # The release recipe never enables proprietary codecs or carries the codec
  # patch. cef-source-build.py applies the same rule before building.
  if ($null -eq $Recipe) { throw 'dependencies.lock.json has no cef.engineSourceBuild recipe.' }
  $taskDefines = @(-split [string]$Recipe.gnDefines)
  $taskCodecDefines = @($taskDefines | Where-Object { $_ -clike 'proprietary_codecs=*' -or $_ -clike 'xenon_windows_codecs=*' })
  if ($Recipe.codecBackend -cne 'none' -or $null -ne $Recipe.patch -or
      $taskCodecDefines.Count -ne 1 -or $taskCodecDefines[0] -cne 'proprietary_codecs=false') {
    throw 'The engine release recipe must keep proprietary codecs disabled.'
  }
}

function Get-XenonEngineArchivePin {
  # $null until a release engine archive is pinned; otherwise the validated pin.
  param([Parameter(Mandatory=$true)]$Lock)
  $taskPin = $Lock.cef.engineArchive
  if ($null -eq $taskPin) { return $null }
  Assert-XenonEngineRecipe $Lock.cef.engineSourceBuild
  if ((@($taskPin.PSObject.Properties.Name) | Sort-Object) -join ',' -cne 'bytes,libcefSha256,sha256,url') {
    throw 'cef.engineArchive must contain exactly url, bytes, sha256 and libcefSha256.'
  }
  foreach ($taskField in 'sha256','libcefSha256') {
    if ([string]$taskPin.$taskField -cnotmatch '^[0-9a-f]{64}$') { throw "cef.engineArchive.$taskField must be a lowercase SHA-256." }
  }
  # GitHub release assets are at most 2 GiB.
  if (-not ($taskPin.bytes -is [int] -or $taskPin.bytes -is [long]) -or $taskPin.bytes -lt 1 -or $taskPin.bytes -gt 2147483648) {
    throw 'cef.engineArchive.bytes must be the archive size in bytes, at most 2 GiB.'
  }
  $taskExpected = Get-XenonEngineArchiveName -Lock $Lock -LibcefSha256 $taskPin.libcefSha256
  if ([string]$taskPin.url -cne $taskExpected.Url) { throw "cef.engineArchive.url must be $($taskExpected.Url)" }
  [pscustomobject]@{ Url=$taskExpected.Url; Name=$taskExpected.Name; Tag=$taskExpected.Tag; Bytes=[long]$taskPin.bytes
    Sha256=$taskPin.sha256; LibcefSha256=$taskPin.libcefSha256 }
}

function Get-XenonEngineProvenance {
  [CmdletBinding()]
  param(
    # Directory holding the provenance manifest, if any.
    [Parameter(Mandatory=$true)][string]$Directory,
    # The libcef.dll the manifest must describe.
    [Parameter(Mandatory=$true)][string]$Engine,
    [Parameter(Mandatory=$true)]$Lock,
    # Directory is a complete CEF distribution: check every file the manifest lists.
    [switch]$Distribution,
    # Require the engine dependencies.lock.json designates for published releases.
    [switch]$Release
  )
  $ErrorActionPreference = 'Stop'
  $taskKinds = @(
    @{ Kind='codec'; File='xenon-codec-build.json'; Recipe=$Lock.cef.mediaSourceBuild },
    @{ Kind='engine'; File='xenon-engine-build.json'; Recipe=$Lock.cef.engineSourceBuild }
  )
  $taskFound = @($taskKinds | Where-Object { Test-Path -LiteralPath (Join-Path $Directory $_.File) })
  if ($taskFound.Count -gt 1) { throw 'A CEF engine must not carry both codec and engine provenance manifests.' }
  $taskHash = (Get-FileHash -LiteralPath $Engine -Algorithm SHA256).Hash.ToLowerInvariant()
  $taskKind = 'stock'
  $taskManifest = $null
  if ($taskFound.Count) {
    $taskKind = $taskFound[0].Kind
    $taskManifest = Join-Path $Directory $taskFound[0].File
    $taskRecipe = $taskFound[0].Recipe
    $taskBuild = Get-Content -LiteralPath $taskManifest -Raw | ConvertFrom-Json
    if ($null -eq $taskRecipe -or ($taskBuild.recipe | ConvertTo-Json -Depth 10 -Compress) -cne ($taskRecipe | ConvertTo-Json -Depth 10 -Compress)) {
      throw "The selected $taskKind engine does not match the pinned source build recipe. Rebuild CEF first."
    }
    if ($taskBuild.files.'Release/libcef.dll' -cne $taskHash) { throw "The $taskKind engine provenance does not match libcef.dll." }
    if ($taskKind -eq 'engine') {
      Assert-XenonEngineRecipe $taskRecipe
      if ($taskBuild.recipeName -cne 'engineSourceBuild' -or $taskBuild.codecBackend -cne 'none') { throw 'The engine provenance does not describe the engine recipe.' }
      foreach ($taskFlag in 'proprietaryCodecs','bundledH264AacDecoders','bundledOpenH264') {
        if ($taskBuild.$taskFlag -isnot [bool] -or $taskBuild.$taskFlag) { throw "The engine provenance must record $taskFlag as false." }
      }
    }
    if ($Distribution) {
      $taskFiles = @($taskBuild.files.PSObject.Properties)
      if (-not $taskFiles.Count) { throw 'The engine provenance lists no files.' }
      foreach ($taskFile in $taskFiles) {
        if ($taskFile.Name -cnotmatch '^[A-Za-z0-9_-][A-Za-z0-9_.-]*(/[A-Za-z0-9_-][A-Za-z0-9_.-]*)*$') { throw "Unexpected provenance file path: $($taskFile.Name)" }
        $taskPath = Join-Path $Directory $taskFile.Name
        if (-not (Test-Path -LiteralPath $taskPath -PathType Leaf) -or
            (Get-FileHash -LiteralPath $taskPath -Algorithm SHA256).Hash.ToLowerInvariant() -cne $taskFile.Value) {
          throw "CEF distribution file does not match its provenance: $($taskFile.Name)"
        }
      }
    }
  }
  if ($Release) {
    $taskPin = Get-XenonEngineArchivePin -Lock $Lock
    if ($taskPin) {
      if ($taskKind -ne 'engine' -or $taskHash -cne $taskPin.LibcefSha256) {
        throw 'Release packages require the Xenon engine pinned in cef.engineArchive.'
      }
    } elseif ($taskKind -ne 'stock') {
      throw 'Release packages use the stock CEF engine until cef.engineArchive pins a Xenon engine build.'
    }
  }
  [pscustomobject]@{ Kind=$taskKind; Manifest=$taskManifest; LibcefSha256=$taskHash }
}
