param()
$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path -Parent $PSScriptRoot
$taskPin = (Get-Content -LiteralPath (Join-Path $taskRoot 'installer/dependencies.lock.json') -Raw | ConvertFrom-Json).innoSetup
$taskCompiler = Join-Path $taskRoot ".cache/inno-setup/$($taskPin.version)/ISCC.exe"
if (-not (Test-Path -LiteralPath $taskCompiler)) { throw 'Run scripts/bootstrap-installer.ps1 before these isolated installer tests.' }
$taskId = [Guid]::NewGuid().ToString('N')
$taskStage = Join-Path $taskRoot ".cache/installer-tests-$taskId"
$taskPayload = Join-Path $taskStage 'payload'
$taskDefaultInstall = Join-Path $taskStage 'Programs/Xenon Fixture'
$taskInstall = Join-Path $taskStage 'Chosen Apps/Xenon Custom Folder'
$taskProfile = Join-Path $taskStage 'LocalAppData/Xenon Browser'
$taskKey = "HKCU:\Software\XenonInstallerTests\$taskId"
$taskUninstallKey = "HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\Xenon.InstallerFixture.$($taskId)_is1"
$taskGroup = Join-Path ([Environment]::GetFolderPath('Programs')) "Xenon Installer Fixture $taskId"
$taskDesktop = Join-Path ([Environment]::GetFolderPath('Desktop')) "Xenon Installer Fixture $taskId.lnk"
$taskResults = [Collections.Generic.List[object]]::new()
$taskMutex = $null
$taskLock = $null
$taskRestricted = $null
$taskOriginalAcl = $null
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
  # Inno's uninstall launcher may exit before its cleanup child. Wait for our
  # own setup marker, never for unrelated installers/processes, before next run.
  $taskDeadline = [DateTime]::UtcNow.AddSeconds(5)
  do {
    try { $taskPendingMutex = [Threading.Mutex]::OpenExisting("Local\XenonInstallerFixtureSetup-$taskId") }
    catch [Threading.WaitHandleCannotBeOpenedException] { break }
    $taskPendingMutex.Dispose()
    if ([DateTime]::UtcNow -gt $taskDeadline) { throw "Fixture $Label setup marker did not close." }
    Start-Sleep -Milliseconds 50
  } while ($true)
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
    & $taskCompiler '/Qp' '/DTestBuild=1' "/DTestId=$taskId" "/DTestInstallDir=$taskDefaultInstall" "/DTestProfileDir=$taskProfile" "/DPayloadDir=$taskPayload" "/DReleaseVersion=$taskVersion" '/DReleaseNumericVersion=0.1.0.0' "/DOutputDirectory=$taskStage" (Join-Path $taskRoot 'installer/xenon.iss')
    if ($LASTEXITCODE -ne 0) { throw "Fixture compilation failed: $taskVersion" }
  }
  $taskOld = Join-Path $taskStage 'fixture-0.1.0-alpha.9.exe'
  $taskNew = Join-Path $taskStage 'fixture-0.1.0-alpha.10.exe'
  $taskStable = Join-Path $taskStage 'fixture-0.1.0.exe'
  $taskOccupied = Join-Path $taskStage 'Unrelated Files'
  New-Item -ItemType Directory -Path $taskOccupied | Out-Null
  [IO.File]::WriteAllText((Join-Path $taskOccupied 'Xenon.exe'),'UNRELATED_FILE_MUST_SURVIVE')
  $taskDenied = @{
    'profile'=$taskProfile
    'profile-child'=(Join-Path $taskProfile 'Programs')
    'profile-ancestor'=(Split-Path -Parent $taskProfile)
    'occupied'=$taskOccupied
    'drive-root'=([IO.Path]::GetPathRoot($taskStage) + '.')
  }
  foreach ($taskCase in $taskDenied.GetEnumerator()) {
    Assert-Fixture ((Invoke-Fixture $taskOld ('denied-' + $taskCase.Key) @('/DIR="' + $taskCase.Value + '"')) -ne 0) "fresh install rejects $($taskCase.Key) destination"
  }
  Assert-Fixture ((Get-Content -LiteralPath (Join-Path $taskOccupied 'Xenon.exe') -Raw) -ceq 'UNRELATED_FILE_MUST_SURVIVE') 'unrelated folder is not overwritten'
  $taskRestricted = Join-Path $taskStage 'Unlistable Folder'
  New-Item -ItemType Directory -Path $taskRestricted | Out-Null
  [IO.File]::WriteAllText((Join-Path $taskRestricted 'Xenon.exe'),'UNLISTABLE_FILE_MUST_SURVIVE')
  $taskOriginalAcl = Get-Acl -LiteralPath $taskRestricted
  $taskDenyAcl = Get-Acl -LiteralPath $taskRestricted
  $taskDenyAcl.AddAccessRule([Security.AccessControl.FileSystemAccessRule]::new([Security.Principal.WindowsIdentity]::GetCurrent().User,[Security.AccessControl.FileSystemRights]::ListDirectory,[Security.AccessControl.AccessControlType]::Deny))
  Set-Acl -LiteralPath $taskRestricted -AclObject $taskDenyAcl
  Assert-Fixture ((Invoke-Fixture $taskOld 'denied-enumeration' @('/DIR="' + $taskRestricted + '"')) -ne 0) 'fresh install fails closed when folder enumeration is denied'
  Assert-Fixture ((Get-Content -LiteralPath (Join-Path $taskStage 'denied-enumeration.log') -Raw).Contains('cannot verify the selected folder is empty')) 'enumeration denial is distinguished from an empty folder'
  Set-Acl -LiteralPath $taskRestricted -AclObject $taskOriginalAcl
  $taskOriginalAcl = $null
  Assert-Fixture ((Get-Content -LiteralPath (Join-Path $taskRestricted 'Xenon.exe') -Raw) -ceq 'UNLISTABLE_FILE_MUST_SURVIVE') 'unlistable folder contents are not replaced'
  Assert-Fixture ((Invoke-Fixture $taskOld 'install' @('/DIR="' + $taskInstall + '"')) -eq 0) 'per-user first install accepts a chosen path containing spaces'
  Assert-Fixture ((Get-Content -LiteralPath (Join-Path $taskStage 'install.log') -Raw).Contains('Xenon destination page: enabled for new installation.')) 'destination chooser is enabled for fresh installation'
  Assert-Fixture (-not (Test-Path -LiteralPath $taskDefaultInstall)) 'custom installation does not create a second default copy'
  Assert-Fixture ((Read-FixtureRelease) -ceq '0.1.0-alpha.9') 'installed release recorded in isolated HKCU key'
  Assert-Fixture ((Test-Path -LiteralPath $taskUninstallKey) -and (Test-Path -LiteralPath (Join-Path $taskGroup "Xenon Installer Fixture $taskId.lnk"))) 'uninstall registration and Start Menu shortcut'
  $taskRegistration = Get-ItemProperty -LiteralPath $taskUninstallKey
  Assert-Fixture ($taskRegistration.'Inno Setup: App Path' -eq $taskInstall -and $taskRegistration.UninstallString.Contains($taskInstall)) 'uninstaller registration records the chosen folder'
  $taskShell = New-Object -ComObject WScript.Shell
  $taskShortcut = $taskShell.CreateShortcut((Join-Path $taskGroup "Xenon Installer Fixture $taskId.lnk"))
  Assert-Fixture ($taskShortcut.TargetPath -eq (Join-Path $taskInstall 'Xenon.exe') -and $taskShortcut.WorkingDirectory -eq $taskInstall) 'Start Menu shortcut uses the chosen executable and working directory'
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
  Assert-Fixture ((Invoke-Fixture $taskNew 'directory-override' @('/DIR="' + (Join-Path $taskStage 'wrong-directory') + '"')) -ne 0) 'upgrade rejects relocation to prevent orphan binaries'
  Assert-Fixture ((Get-Content -LiteralPath (Join-Path $taskStage 'directory-override.log') -Raw).Contains('uninstall it first')) 'relocation refusal explains uninstall and reinstall'
  Remove-ItemProperty -LiteralPath $taskUninstallKey -Name 'Inno Setup: App Path'
  Assert-Fixture ((Invoke-Fixture $taskNew 'missing-registered-path') -ne 0) 'missing standard registered path fails closed instead of choosing a conflicting default'
  Set-ItemProperty -LiteralPath $taskUninstallKey -Name 'Inno Setup: App Path' -Value $taskInstall
  Assert-Fixture ((Invoke-Fixture $taskNew 'force-close-override' @('/FORCECLOSEAPPLICATIONS')) -ne 0) 'force-close command override rejected'
  Assert-Fixture ((Invoke-Fixture $taskNew 'upgrade' @('/TASKS=desktopicon')) -eq 0) 'alpha.9 to alpha.10 upgrade succeeds'
  Assert-Fixture ((Get-Content -LiteralPath (Join-Path $taskStage 'upgrade.log') -Raw).Contains('Xenon destination page: existing installation retained.')) 'upgrade skips folder selection and remembers the chosen path without /DIR'
  Assert-Fixture (((Read-FixtureRelease) -ceq '0.1.0-alpha.10') -and ((Get-Content -LiteralPath (Join-Path $taskInstall 'release.txt') -Raw) -ceq '0.1.0-alpha.10')) 'upgrade replaces payload and release together'
  Assert-Fixture (Test-Path -LiteralPath $taskDesktop) 'explicit desktop shortcut created'
  Assert-Fixture ($taskShell.CreateShortcut($taskDesktop).TargetPath -eq (Join-Path $taskInstall 'Xenon.exe')) 'desktop shortcut points to the custom installation'
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
  Assert-Fixture (-not (Test-Path -LiteralPath (Join-Path $taskInstall 'Xenon.exe'))) 'custom runtime removed by its uninstaller'
  # Alpha.10 already writes these standard Inno registry values, but no custom
  # InstallDir value. Retain that compatibility rather than depending on new data.
  $taskInstall = $taskDefaultInstall
  $taskUninstaller = Join-Path $taskInstall 'unins000.exe'
  New-Item -ItemType Directory -Path $taskInstall -Force | Out-Null
  Assert-Fixture ((Invoke-Fixture $taskOld 'default-install') -eq 0) 'fresh install still accepts the default empty folder'
  $taskRegistration = Get-ItemProperty -LiteralPath $taskUninstallKey
  Assert-Fixture ($taskRegistration.'Inno Setup: App Path' -eq $taskInstall -and -not ($taskRegistration.PSObject.Properties.Name -contains 'InstallDir')) 'legacy registration uses standard Inno app path without new InstallDir metadata'
  Assert-Fixture ((Invoke-Fixture $taskNew 'default-upgrade') -eq 0) 'existing default installation upgrades in place without /DIR'
  Assert-Fixture ((Get-Content -LiteralPath (Join-Path $taskInstall 'release.txt') -Raw) -ceq '0.1.0-alpha.10') 'default upgrade retains its registered folder'
  Assert-Fixture ((Invoke-Fixture $taskUninstaller 'default-uninstall') -eq 0) 'default fixture cleanup succeeds'
  Assert-Fixture ((Get-Content -LiteralPath (Join-Path $taskProfile 'profile-sentinel.txt') -Raw) -ceq 'SYNTHETIC_PROFILE_MUST_SURVIVE') 'separate synthetic browser profile survives upgrade and uninstall'
} finally {
  if ($taskOriginalAcl -and $taskRestricted) { Set-Acl -LiteralPath $taskRestricted -AclObject $taskOriginalAcl }
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
