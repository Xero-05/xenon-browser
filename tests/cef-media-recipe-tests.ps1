$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path $PSScriptRoot -Parent
$taskRecipe = (Get-Content (Join-Path $taskRoot 'dependencies.lock.json') -Raw | ConvertFrom-Json).cef.mediaSourceBuild
$taskRecipeText = $taskRecipe | ConvertTo-Json -Compress
$taskTokens = $null
$taskErrors = $null
$taskAst = [System.Management.Automation.Language.Parser]::ParseFile(
  (Join-Path $taskRoot 'scripts/build-cef-media.ps1'), [ref]$taskTokens, [ref]$taskErrors)
if ($taskErrors.Count) { throw ($taskErrors | Out-String) }
# Exercise the actual marker gate without downloading dependencies or building CEF.
$taskGate = $taskAst.Find({ param($node)
  $node -is [System.Management.Automation.Language.IfStatementAst] -and
    $node.Clauses[0].Item1.Extent.Text -eq 'Test-Path -LiteralPath $taskWork'
}, $true)
if (-not $taskGate) { throw 'Source-build recipe gate not found.' }
$taskGateScript = [scriptblock]::Create($taskGate.Extent.Text)
$taskPrevious = $taskRecipe | ConvertTo-Json -Depth 10 | ConvertFrom-Json
$taskPrevious.PSObject.Properties.Remove('videoSchedulingPatch')
$taskLegacy = $taskPrevious | ConvertTo-Json -Depth 10 | ConvertFrom-Json
$taskLegacy.PSObject.Properties.Remove('codecBackend')
$taskLegacy.PSObject.Properties.Remove('patch')
$taskLegacy.gnDefines = 'is_official_build=true use_thin_lto=false chrome_pgo_phase=0 symbol_level=0 proprietary_codecs=true ffmpeg_branding=Chrome'
$taskCases = @(
  @{ Name='matching'; Marker=$taskRecipeText; Accept=$true; Backup=$false },
  @{ Name='windows-migration'; Marker=($taskPrevious | ConvertTo-Json -Compress); Scheduling=$true; Accept=$true; Backup=$true },
  @{ Name='legacy-migration'; Marker=($taskLegacy | ConvertTo-Json -Compress); Legacy=$true; Accept=$true; Backup=$true },
  @{ Name='missing-opt-in'; Marker=($taskPrevious | ConvertTo-Json -Compress); Accept=$false },
  @{ Name='wrong-switch'; Marker=($taskPrevious | ConvertTo-Json -Compress); Legacy=$true; Accept=$false },
  @{ Name='unrelated'; Marker='{"chromiumCommit":"different"}'; Scheduling=$true; Accept=$false },
  @{ Name='missing-marker'; Scheduling=$true; Accept=$false }
)
# Keep synthetic evidence in the ignored workspace; never remove user directories.
$taskSuite = Join-Path $taskRoot ('.cache/cef-recipe-tests-' + [guid]::NewGuid().ToString('N'))
foreach ($taskCase in $taskCases) {
  $taskWork = Join-Path $taskSuite $taskCase.Name
  New-Item -ItemType Directory -Path $taskWork -Force | Out-Null
  $taskMarker = Join-Path $taskWork 'xenon-media-build.json'
  if ($taskCase.Marker) { [IO.File]::WriteAllText($taskMarker, $taskCase.Marker) }
  $MigrateToWindowsCodecs = [bool]$taskCase.Legacy
  $UpdateVideoScheduling = [bool]$taskCase.Scheduling
  $BuildOnly = $true
  $taskFailure = $null
  try { & $taskGateScript } catch { $taskFailure = $_ }
  if ($taskCase.Accept -eq [bool]$taskFailure) { throw "Unexpected result for $($taskCase.Name): $taskFailure" }
  if ($taskCase.Accept) {
    if ((Get-Content $taskMarker -Raw) -cne $taskRecipeText) { throw 'New recipe not recorded.' }
    $taskBackups = @(Get-ChildItem -LiteralPath $taskWork -Filter '*.previous')
    if ([bool]$taskBackups.Count -ne [bool]$taskCase.Backup) { throw 'Incorrect backup behavior.' }
    if ($taskCase.Backup -and (Get-Content $taskBackups[0].FullName -Raw) -cne $taskCase.Marker) { throw 'Previous marker not preserved.' }
  } elseif ($taskCase.Marker) {
    if ((Get-Content $taskMarker -Raw) -cne $taskCase.Marker) { throw 'Rejected marker changed.' }
  } elseif (Test-Path $taskMarker) { throw 'Missing marker was created.' }
}
Write-Output "$($taskCases.Count) CEF recipe migration checks passed."
