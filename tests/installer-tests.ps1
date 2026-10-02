param()
$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path -Parent $PSScriptRoot
$taskPin = (Get-Content -LiteralPath (Join-Path $taskRoot 'installer/dependencies.lock.json') -Raw | ConvertFrom-Json).innoSetup
$taskCompiler = Join-Path $taskRoot ".cache/inno-setup/$($taskPin.version)/ISCC.exe"
if (-not (Test-Path -LiteralPath $taskCompiler)) { throw 'Run scripts/bootstrap-installer.ps1 before these isolated installer tests.' }
$taskId = [Guid]::NewGuid().ToString('N')
$taskStage = Join-Path $taskRoot ".cache/installer-tests-$taskId"
$taskPayload = Join-Path $taskStage 'payload'
$taskInstall = Join-Path $taskStage 'Programs/Xenon Fixture'
$taskProfile = Join-Path $taskStage 'LocalAppData/Xenon Browser'
$taskKey = "HKCU:\Software\XenonInstallerTests\$taskId"
$taskUninstallKey = "HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\Xenon.InstallerFixture.$($taskId)_is1"
$taskGroup = Join-Path ([Environment]::GetFolderPath('Programs')) "Xenon Installer Fixture $taskId"
$taskDesktop = Join-Path ([Environment]::GetFolderPath('Desktop')) "Xenon Installer Fixture $taskId.lnk"
$taskResults = [Collections.Generic.List[object]]::new()
$taskMutex = $null
$taskLock = $null
$taskUninstaller = Join-Path $taskInstall 'unins000.exe'
function Assert-Fixture([bool]$Condition,[string]$Name) {
  if (-not $Condition) { throw "Installer regression failed: $Name" }
  $taskResults.Add(@{name=$Name;passed=$true})
  Write-Host "PASS $Name"
}
function Invoke-Fixture([string]$Executable,[string]$Label,[string[]]$Extra=@()) {
  $taskLog = Join-Path $taskStage ($Label + '.log')
  $taskArgs = @('/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART','/SP-',('/LOG="' + $taskLog + '"')) + $Extra
  $taskProcess = Start-Process -FilePath $Executable -ArgumentList $taskArgs -WindowStyle Hidden -PassThru
  if (-not $taskProcess.WaitForExit(30000)) { $taskProcess.Kill(); throw "Fixture $Label exceeded 30 seconds." }
  return $taskProcess.ExitCode
}
function Read-FixtureRelease { return (Get-ItemProperty -LiteralPath $taskKey -Name ReleaseVersion).ReleaseVersion }
New-Item -ItemType Directory -Path (Join-Path $taskPayload 'runtime'),$taskProfile | Out-Null
[IO.File]::WriteAllText((Join-Path $taskProfile 'profile-sentinel.txt'),'SYNTHETIC_PROFILE_MUST_SURVIVE')
[IO.File]::WriteAllText((Join-Path $taskPayload 'LICENSE'),'Synthetic installer regression payload; not a browser.')
foreach ($taskFile in @('Xenon.exe','Xenon.dll','libcef.dll','runtime/node.exe')) { [IO.File]::WriteAllText((Join-Path $taskPayload $taskFile),'SYNTHETIC_RUNTIME_FIXTURE') }
try {
  foreach ($taskVersion in @('0.1.0-alpha.9','0.1.0-alpha.10','0.1.0')) {
    [IO.File]::WriteAllText((Join-Path $taskPayload 'release.txt'),$taskVersion)
    & $taskCompiler '/Qp' '/DTestBuild=1' "/DTestId=$taskId" "/DTestInstallDir=$taskInstall" "/DPayloadDir=$taskPayload" "/DReleaseVersion=$taskVersion" '/DReleaseNumericVersion=0.1.0.0' "/DOutputDirectory=$taskStage" (Join-Path $taskRoot 'installer/xenon.iss')
    if ($LASTEXITCODE -ne 0) { throw "Fixture compilation failed: $taskVersion" }
  }
  $taskOld = Join-Path $taskStage 'fixture-0.1.0-alpha.9.exe'
  $taskNew = Join-Path $taskStage 'fixture-0.1.0-alpha.10.exe'
  $taskStable = Join-Path $taskStage 'fixture-0.1.0.exe'
  Assert-Fixture ((Invoke-Fixture $taskOld 'install') -eq 0) 'per-user first install'
  Assert-Fixture ((Read-FixtureRelease) -ceq '0.1.0-alpha.9') 'installed release recorded in isolated HKCU key'
  Assert-Fixture ((Test-Path -LiteralPath $taskUninstallKey) -and (Test-Path -LiteralPath (Join-Path $taskGroup "Xenon Installer Fixture $taskId.lnk"))) 'uninstall registration and Start Menu shortcut'
  Assert-Fixture (-not (Test-Path -LiteralPath $taskDesktop)) 'desktop shortcut is opt-in'
  $taskMutex = [Threading.Mutex]::new($false,"Local\XenonInstallerFixtureRunning-$taskId")
  Assert-Fixture ((Invoke-Fixture $taskNew 'running-upgrade') -ne 0) 'running browser marker refuses upgrade'
  Assert-Fixture ((Invoke-Fixture $taskUninstaller 'running-uninstall') -ne 0) 'running browser marker refuses uninstall'
  $taskMutex.Dispose(); $taskMutex = $null
  Assert-Fixture ((Read-FixtureRelease) -ceq '0.1.0-alpha.9') 'running refusals leave release unchanged'
  $taskLock = [IO.File]::Open((Join-Path $taskInstall 'runtime/node.exe'),[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::Read)
  Assert-Fixture ((Invoke-Fixture $taskNew 'locked-node-upgrade') -ne 0) 'held MCP Node runtime refuses upgrade before replacement'
  Assert-Fixture ((Invoke-Fixture $taskUninstaller 'locked-node-uninstall') -ne 0) 'held MCP Node runtime refuses uninstall'
  $taskLock.Dispose(); $taskLock = $null
  Assert-Fixture ((Read-FixtureRelease) -ceq '0.1.0-alpha.9') 'locked-file refusals leave release unchanged'
  Assert-Fixture ((Invoke-Fixture $taskNew 'directory-override' @('/DIR="' + (Join-Path $taskStage 'wrong-directory') + '"')) -ne 0) 'fixed install root rejects directory override'
  Assert-Fixture ((Invoke-Fixture $taskNew 'force-close-override' @('/FORCECLOSEAPPLICATIONS')) -ne 0) 'force-close command override rejected'
  Assert-Fixture ((Invoke-Fixture $taskNew 'upgrade' @('/TASKS=desktopicon')) -eq 0) 'alpha.9 to alpha.10 upgrade succeeds'
  Assert-Fixture (((Read-FixtureRelease) -ceq '0.1.0-alpha.10') -and ((Get-Content -LiteralPath (Join-Path $taskInstall 'release.txt') -Raw) -ceq '0.1.0-alpha.10')) 'upgrade replaces payload and release together'
  Assert-Fixture (Test-Path -LiteralPath $taskDesktop) 'explicit desktop shortcut created'
  Assert-Fixture ((Invoke-Fixture $taskOld 'downgrade') -ne 0) 'alpha.10 to alpha.9 downgrade rejected numerically'
  foreach ($taskInvalid in @('not-a-release','0.1.0.-alpha.9','0.1.0-alpha.09')) {
    Set-ItemProperty -LiteralPath $taskKey -Name ReleaseVersion -Value $taskInvalid
    Assert-Fixture ((Invoke-Fixture $taskNew ('invalid-record-' + $taskInvalid)) -ne 0) "invalid installed release fails closed: $taskInvalid"
  }
  Set-ItemProperty -LiteralPath $taskKey -Name ReleaseVersion -Value '0.1.0-alpha.10'
  Assert-Fixture ((Invoke-Fixture $taskStable 'stable-upgrade') -eq 0) 'prerelease to stable upgrade succeeds'
  Assert-Fixture ((Invoke-Fixture $taskNew 'stable-downgrade') -ne 0) 'stable to prerelease downgrade rejected'
  Assert-Fixture ((Invoke-Fixture $taskUninstaller 'uninstall') -eq 0) 'uninstall succeeds when runtime is closed'
  Assert-Fixture ((-not (Test-Path -LiteralPath $taskUninstallKey)) -and (-not (Test-Path -LiteralPath $taskKey)) -and (-not (Test-Path -LiteralPath $taskGroup)) -and (-not (Test-Path -LiteralPath $taskDesktop))) 'uninstall removes only fixture registration and shortcuts'
  Assert-Fixture ((Get-Content -LiteralPath (Join-Path $taskProfile 'profile-sentinel.txt') -Raw) -ceq 'SYNTHETIC_PROFILE_MUST_SURVIVE') 'separate synthetic browser profile survives upgrade and uninstall'
} finally {
  if ($taskLock) { $taskLock.Dispose() }
  if ($taskMutex) { $taskMutex.Dispose() }
  # These identifiers were generated above for this run; never remove product
  # registry keys, real browser data, or arbitrary pre-existing directories.
  if (Test-Path -LiteralPath $taskUninstaller) { $null = Invoke-Fixture $taskUninstaller 'cleanup' }
  if (Test-Path -LiteralPath $taskKey) { Remove-Item -LiteralPath $taskKey -Recurse -Force }
  if (Test-Path -LiteralPath $taskUninstallKey) { Remove-Item -LiteralPath $taskUninstallKey -Recurse -Force }
  if (Test-Path -LiteralPath $taskGroup) {
    if ([IO.Path]::GetDirectoryName([IO.Path]::GetFullPath($taskGroup)) -ne [IO.Path]::GetFullPath([Environment]::GetFolderPath('Programs')) -or [IO.Path]::GetFileName($taskGroup) -cne "Xenon Installer Fixture $taskId") { throw 'Unsafe fixture shortcut cleanup.' }
    Remove-Item -LiteralPath $taskGroup -Recurse -Force
  }
  if (Test-Path -LiteralPath $taskDesktop) { Remove-Item -LiteralPath $taskDesktop -Force }
  @{ fixtureId=$taskId; results=@($taskResults.ToArray()); passed=$taskResults.Count; source='same Inno policy with isolated test-only identity, directory and mutex'; profile='synthetic fixture only' } | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $taskStage 'results.json') -Encoding utf8
  Write-Host "Installer regression artifacts: $taskStage"
}
