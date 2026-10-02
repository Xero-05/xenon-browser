param([Parameter(Mandatory=$true)][string]$Zip)
$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path -Parent $PSScriptRoot
$taskVersion = (Get-Content -LiteralPath (Join-Path $taskRoot 'VERSION') -Raw).Trim()
if ($taskVersion -notmatch '^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)(?:-(?:alpha|beta|rc)\.(0|[1-9][0-9]*))?$') { throw 'VERSION is not a supported release version.' }
$taskNumbers = $taskVersion.Split('-')[0].Split('.')
foreach ($taskNumber in $taskNumbers) { if ([decimal]$taskNumber -gt 65535) { throw 'Release version exceeds the Windows version field range.' } }
$taskZip = [IO.Path]::GetFullPath($Zip)
$taskPackageName = "Xenon-$taskVersion-windows-x64-unsigned"
if ([IO.Path]::GetFileName($taskZip) -cne ($taskPackageName + '.zip')) { throw 'The package filename must match VERSION.' }
# Complete checksum, inventory, private-file, and individual-file validation
# happens before either compiler execution or payload extraction.
& (Join-Path $PSScriptRoot 'verify-package.ps1') -Zip $taskZip
$taskOut = Join-Path $taskRoot 'dist'
$taskOutput = Join-Path $taskOut "Xenon-$taskVersion-windows-x64-setup-unsigned.exe"
if ((Test-Path -LiteralPath $taskOutput) -or (Test-Path -LiteralPath ($taskOutput + '.sha256'))) { throw 'Installer output already exists; refusing to overwrite a release artifact.' }
$taskStage = Join-Path $taskRoot ('.cache/installer-stage-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $taskStage | Out-Null
try {
  [IO.Compression.ZipFile]::ExtractToDirectory($taskZip, $taskStage)
  $taskPayload = Join-Path $taskStage $taskPackageName
  $taskManifest = Get-Content -LiteralPath (Join-Path $taskPayload 'release-manifest.json') -Raw | ConvertFrom-Json
  if ($taskManifest.product -ne 'Xenon Browser' -or $taskManifest.version -cne $taskVersion -or $taskManifest.platform -ne 'windows-x64' -or $taskManifest.signed -ne $false) { throw 'The verified package manifest does not match this installer release.' }
  $taskCompiler = & (Join-Path $PSScriptRoot 'bootstrap-installer.ps1')
  if ($taskCompiler -is [array]) { $taskCompiler = $taskCompiler[-1] }
  $taskArguments = @('/Qp', ('/DPayloadDir=' + $taskPayload), ('/DReleaseVersion=' + $taskVersion), ('/DReleaseNumericVersion=' + ($taskNumbers -join '.') + '.0'), ('/DOutputDirectory=' + $taskOut), (Join-Path $taskRoot 'installer/xenon.iss'))
  & $taskCompiler @taskArguments
  if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $taskOutput)) { throw "Inno Setup compilation failed (exit $LASTEXITCODE)." }
  $taskHash = (Get-FileHash -LiteralPath $taskOutput -Algorithm SHA256).Hash.ToLowerInvariant()
  [IO.File]::WriteAllText($taskOutput + '.sha256', "$taskHash  $([IO.Path]::GetFileName($taskOutput))`n", [Text.UTF8Encoding]::new($false))
  Write-Host "Built unsigned per-user installer: $taskOutput"
  Write-Host "SHA-256: $taskHash"
} finally {
  $taskResolvedStage = [IO.Path]::GetFullPath($taskStage)
  $taskExpectedParent = [IO.Path]::GetFullPath((Join-Path $taskRoot '.cache'))
  if ([IO.Path]::GetDirectoryName($taskResolvedStage) -ne $taskExpectedParent -or [IO.Path]::GetFileName($taskResolvedStage) -notmatch '^installer-stage-[0-9a-f]{32}$') { throw 'Refusing unsafe installer staging cleanup.' }
  Remove-Item -LiteralPath $taskResolvedStage -Recurse -Force
}
