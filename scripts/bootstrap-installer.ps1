param()
$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path -Parent $PSScriptRoot
$taskDependency = (Get-Content -LiteralPath (Join-Path $taskRoot 'installer/dependencies.lock.json') -Raw | ConvertFrom-Json).innoSetup
. (Join-Path $PSScriptRoot 'dependency-download.ps1')
$taskArchive = Get-XenonDependency -Url $taskDependency.url -Name $taskDependency.file -ExpectedHash $taskDependency.sha256 -DownloadDirectory (Join-Path $taskRoot 'third_party/downloads')
$taskSignature = Get-AuthenticodeSignature -LiteralPath $taskArchive
if ($taskSignature.Status -ne 'Valid' -or $taskSignature.SignerCertificate.Subject -notmatch ('(^|,\s*)CN=' + [Regex]::Escape($taskDependency.publisher) + '(,|$)')) {
  throw 'The pinned Inno Setup download does not have its expected valid publisher signature.'
}
$taskParent = Join-Path $taskRoot '.cache/inno-setup'
$taskDestination = Join-Path $taskParent $taskDependency.version
$taskCompiler = Join-Path $taskDestination 'ISCC.exe'
if (Test-Path -LiteralPath $taskCompiler) {
  $taskInstalledSignature = Get-AuthenticodeSignature -LiteralPath $taskCompiler
  if ($taskInstalledSignature.Status -ne 'Valid' -or $taskInstalledSignature.SignerCertificate.Subject -notmatch ('(^|,\s*)CN=' + [Regex]::Escape($taskDependency.publisher) + '(,|$)')) {
    throw 'The cached Inno compiler signature is invalid. Remove its cache directory and bootstrap again.'
  }
  Write-Output $taskCompiler
  return
}
New-Item -ItemType Directory -Force -Path $taskParent | Out-Null
$taskStage = Join-Path $taskParent ($taskDependency.version + '-stage-' + [Guid]::NewGuid().ToString('N'))
# This is upstream's supported portable mode: no uninstaller, registration,
# file association, or shortcuts. /CURRENTUSER prevents an elevation request.
# https://github.com/jrsoftware/issrc/blob/is-7_1_0/isportable.iss
$taskArguments = @('/PORTABLE=1','/CURRENTUSER','/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART','/SP-','/NOICONS',('/DIR="' + $taskStage + '"'))
$taskProcess = Start-Process -FilePath $taskArchive -ArgumentList $taskArguments -WindowStyle Hidden -PassThru
if (-not $taskProcess.WaitForExit(120000)) {
  $taskProcess.Kill()
  throw 'Portable Inno compiler extraction exceeded two minutes.'
}
if ($taskProcess.ExitCode -ne 0 -or -not (Test-Path -LiteralPath (Join-Path $taskStage 'ISCC.exe'))) {
  throw "Portable Inno compiler extraction failed (exit $($taskProcess.ExitCode))."
}
if (Test-Path -LiteralPath $taskDestination) { throw 'An incomplete compiler cache already exists; remove that cache directory before retrying.' }
# Both paths are fresh direct children of the known ignored tool-cache root.
if ([IO.Path]::GetDirectoryName([IO.Path]::GetFullPath($taskStage)) -ne [IO.Path]::GetFullPath($taskParent) -or
    [IO.Path]::GetDirectoryName([IO.Path]::GetFullPath($taskDestination)) -ne [IO.Path]::GetFullPath($taskParent)) { throw 'Unsafe compiler cache path.' }
Move-Item -LiteralPath $taskStage -Destination $taskDestination
Write-Output $taskCompiler
