$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path $PSScriptRoot -Parent
$taskLock = Get-Content (Join-Path $taskRoot 'dependencies.lock.json') -Raw | ConvertFrom-Json
$taskMediaRecipe = $taskLock.cef.mediaSourceBuild
$taskEngineRecipe = $taskLock.cef.engineSourceBuild
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
# The script's own table names each recipe's marker file.
$taskTable = $taskAst.Find({ param($node)
  $node -is [System.Management.Automation.Language.AssignmentStatementAst] -and $node.Left.Extent.Text -eq '$taskRecipes'
}, $true)
if (-not $taskTable) { throw 'Source-build recipe table not found.' }
$taskRecipes = & ([scriptblock]::Create($taskTable.Right.Extent.Text))
if ($taskRecipes.Media.Marker -eq $taskRecipes.Engine.Marker) { throw 'Recipes must use separate work-directory markers.' }
function Get-TaskVariant($Recipe, [string[]]$Remove) {
  $taskVariant = $Recipe | ConvertTo-Json -Depth 10 | ConvertFrom-Json
  foreach ($taskName in $Remove) { $taskVariant.PSObject.Properties.Remove($taskName) }
  $taskVariant
}
$taskPrevious = Get-TaskVariant $taskMediaRecipe @('videoSchedulingPatch','backgroundFrameRatePatch')
$taskScheduling = Get-TaskVariant $taskMediaRecipe @('backgroundFrameRatePatch')
$taskLegacy = Get-TaskVariant $taskPrevious @('codecBackend','patch')
$taskLegacy.gnDefines = 'is_official_build=true use_thin_lto=false chrome_pgo_phase=0 symbol_level=0 proprietary_codecs=true ffmpeg_branding=Chrome'
$taskEngineOnePatch = Get-TaskVariant $taskEngineRecipe @('backgroundFrameRatePatch')
$taskEngineNoPatches = Get-TaskVariant $taskEngineRecipe @('videoSchedulingPatch','backgroundFrameRatePatch')
$taskEngineChangedPatch = Get-TaskVariant $taskEngineRecipe @('backgroundFrameRatePatch')
$taskEngineChangedPatch.videoSchedulingPatch.sha256 = '0' * 64
$taskEngineOtherSource = Get-TaskVariant $taskEngineRecipe @('backgroundFrameRatePatch')
$taskEngineOtherSource.chromiumCommit = '0' * 40
$taskCases = @(
  @{ Name='matching'; Marker=($taskMediaRecipe | ConvertTo-Json -Compress); Accept=$true; Backup=$false },
  @{ Name='windows-migration'; Marker=($taskPrevious | ConvertTo-Json -Compress); Scheduling=$true; Accept=$true; Backup=$true },
  @{ Name='frame-rate-migration'; Marker=($taskScheduling | ConvertTo-Json -Compress); FrameRate=$true; Accept=$true; Backup=$true },
  @{ Name='frame-rate-wrong-switch'; Marker=($taskScheduling | ConvertTo-Json -Compress); Scheduling=$true; Accept=$false },
  @{ Name='legacy-migration'; Marker=($taskLegacy | ConvertTo-Json -Compress); Legacy=$true; Accept=$true; Backup=$true },
  @{ Name='missing-opt-in'; Marker=($taskPrevious | ConvertTo-Json -Compress); Accept=$false },
  @{ Name='wrong-switch'; Marker=($taskPrevious | ConvertTo-Json -Compress); Legacy=$true; Accept=$false },
  @{ Name='media-add-patches'; Marker=($taskScheduling | ConvertTo-Json -Compress); AddPatches=$true; Accept=$false },
  @{ Name='unrelated'; Marker='{"chromiumCommit":"different"}'; Scheduling=$true; Accept=$false },
  @{ Name='missing-marker'; Scheduling=$true; Accept=$false },
  # Media markers never satisfy the engine gate, or the reverse.
  @{ Name='media-in-engine-directory'; Other=($taskEngineRecipe | ConvertTo-Json -Compress); Scheduling=$true; Accept=$false },
  @{ Name='engine-matching'; Engine=$true; Marker=($taskEngineRecipe | ConvertTo-Json -Compress); Accept=$true; Backup=$false },
  @{ Name='engine-add-patch'; Engine=$true; Marker=($taskEngineOnePatch | ConvertTo-Json -Compress); AddPatches=$true; Accept=$true; Backup=$true },
  @{ Name='engine-add-all-patches'; Engine=$true; Marker=($taskEngineNoPatches | ConvertTo-Json -Compress); AddPatches=$true; Accept=$true; Backup=$true },
  @{ Name='engine-add-missing-opt-in'; Engine=$true; Marker=($taskEngineOnePatch | ConvertTo-Json -Compress); Accept=$false },
  @{ Name='engine-codec-switch'; Engine=$true; Marker=($taskEngineOnePatch | ConvertTo-Json -Compress); FrameRate=$true; AddPatches=$true; Accept=$false },
  @{ Name='engine-changed-patch'; Engine=$true; Marker=($taskEngineChangedPatch | ConvertTo-Json -Compress); AddPatches=$true; Accept=$false },
  @{ Name='engine-other-source'; Engine=$true; Marker=($taskEngineOtherSource | ConvertTo-Json -Compress); AddPatches=$true; Accept=$false },
  @{ Name='engine-in-codec-directory'; Engine=$true; Other=($taskMediaRecipe | ConvertTo-Json -Compress); AddPatches=$true; Accept=$false },
  @{ Name='engine-codec-recipe-marker'; Engine=$true; Marker=($taskMediaRecipe | ConvertTo-Json -Compress); AddPatches=$true; Accept=$false }
)
# Keep synthetic evidence in the ignored workspace; never remove user directories.
$taskSuite = Join-Path $taskRoot ('.cache/cef-recipe-tests-' + [guid]::NewGuid().ToString('N'))
foreach ($taskCase in $taskCases) {
  $taskWork = Join-Path $taskSuite $taskCase.Name
  New-Item -ItemType Directory -Path $taskWork -Force | Out-Null
  $Recipe = if ($taskCase.Engine) { 'Engine' } else { 'Media' }
  $taskRecipe = if ($taskCase.Engine) { $taskEngineRecipe } else { $taskMediaRecipe }
  $taskRecipeText = $taskRecipe | ConvertTo-Json -Compress
  $taskMarker = Join-Path $taskWork $taskRecipes[$Recipe].Marker
  $taskOtherMarker = Join-Path $taskWork $taskRecipes[$(if ($taskCase.Engine) { 'Media' } else { 'Engine' })].Marker
  if ($taskCase.Marker) { [IO.File]::WriteAllText($taskMarker, $taskCase.Marker) }
  if ($taskCase.Other) { [IO.File]::WriteAllText($taskOtherMarker, $taskCase.Other) }
  $MigrateToWindowsCodecs = [bool]$taskCase.Legacy
  $UpdateVideoScheduling = [bool]$taskCase.Scheduling
  $UpdateBackgroundFrameRate = [bool]$taskCase.FrameRate
  $AddPatches = [bool]$taskCase.AddPatches
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
  if ($taskCase.Other -and (Get-Content $taskOtherMarker -Raw) -cne $taskCase.Other) { throw 'The other recipe marker changed.' }
}
Write-Output "$($taskCases.Count) CEF recipe migration checks passed."
