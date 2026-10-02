; Compiled only from the checksum-verified runtime ZIP by build-installer.ps1.
#ifndef PayloadDir
  #error PayloadDir is required
#endif
#ifndef ReleaseVersion
  #error ReleaseVersion is required
#endif
#ifndef ReleaseNumericVersion
  #error ReleaseNumericVersion is required
#endif
#ifndef OutputDirectory
  #error OutputDirectory is required
#endif
#ifdef TestBuild
  ; Only the dedicated test harness defines these. The public helper never does.
  #ifndef TestId
    #error TestId is required for isolated installer tests
  #endif
  #ifndef TestInstallDir
    #error TestInstallDir is required for isolated installer tests
  #endif
  #define ProductId "Xenon.InstallerFixture." + TestId
  #define ProductName "Xenon Installer Fixture " + TestId
  #define ProductDir TestInstallDir
  #define ProductRegistry "Software\XenonInstallerTests\" + TestId
  #define RunningMutex "Local\XenonInstallerFixtureRunning-" + TestId
  #define InstallerMutex "Local\XenonInstallerFixtureSetup-" + TestId
  #define OutputName "fixture-" + ReleaseVersion
#else
  #define ProductId "{{C0D7CF52-A672-4B76-82E4-BE1664F9D305}"
  #define ProductName "Xenon Browser"
  #define ProductDir "{localappdata}\Programs\Xenon Browser"
  #define ProductRegistry "Software\Xenon Browser\Installer"
  #define RunningMutex "Local\XenonBrowserRunning"
  #define InstallerMutex "Local\XenonBrowserSetup"
  #define OutputName "Xenon-" + ReleaseVersion + "-windows-x64-setup-unsigned"
#endif

[Setup]
AppId={#ProductId}
AppName={#ProductName}
AppVersion={#ReleaseVersion}
AppVerName={#ProductName} {#ReleaseVersion}
AppPublisher=Xenon Browser contributors
AppPublisherURL=https://github.com/Xero-05/xenon-browser
AppSupportURL=https://github.com/Xero-05/xenon-browser/issues
AppUpdatesURL=https://github.com/Xero-05/xenon-browser/releases
VersionInfoVersion={#ReleaseNumericVersion}
VersionInfoDescription={#ProductName} per-user installer (unsigned alpha)
DefaultDirName={#ProductDir}
DefaultGroupName={#ProductName}
DisableDirPage=yes
DisableProgramGroupPage=yes
UsePreviousAppDir=no
PrivilegesRequired=lowest
SetupArchitecture=x64
ArchitecturesAllowed=x64os
MinVersion=10.0
AppMutex={#RunningMutex}
SetupMutex={#InstallerMutex}
CloseApplications=no
RestartApplications=no
AlwaysRestart=no
UninstallRestartComputer=no
WizardStyle=modern
SetupIconFile=..\assets\branding\xenon-icon.ico
UninstallDisplayIcon={app}\Xenon.exe
LicenseFile={#PayloadDir}\LICENSE
OutputDir={#OutputDirectory}
OutputBaseFilename={#OutputName}
Compression=lzma2/fast
SolidCompression=yes
DiskSpanning=no
AllowNoIcons=yes
DisableWelcomePage=no

[Tasks]
Name: desktopicon; Description: "Create a desktop shortcut"; Flags: unchecked

[Files]
Source: "{#PayloadDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\{#ProductName}"; Filename: "{app}\Xenon.exe"; WorkingDir: "{app}"; AppUserModelID: "Xenon.Browser"
Name: "{autodesktop}\{#ProductName}"; Filename: "{app}\Xenon.exe"; WorkingDir: "{app}"; AppUserModelID: "Xenon.Browser"; Tasks: desktopicon

[Registry]
Root: HKCU; Subkey: "{#ProductRegistry}"; ValueType: string; ValueName: "ReleaseVersion"; ValueData: "{#ReleaseVersion}"; Flags: uninsdeletevalue uninsdeletekeyifempty

[Code]
#include "version.iss"

function OpenFileForUpdate(Name: String; Access, Share: DWORD; Security: NativeInt;
  Creation, Flags: DWORD; Template: THandle): THandle;
  external 'CreateFileW@kernel32.dll stdcall';
function ReleaseFileHandle(Handle: THandle): Boolean;
  external 'CloseHandle@kernel32.dll stdcall';
function CreateInstallerMutex(Security: NativeInt; InitialOwner: Boolean; Name: String): THandle;
  external 'CreateMutexW@kernel32.dll stdcall';
function LastWindowsError: DWORD;
  external 'GetLastError@kernel32.dll stdcall';

var UninstallMutex: THandle;

function VersionError: String;
var Existing: String;
begin
  Result := '';
  if not ValidRelease('{#ReleaseVersion}') then begin Result := 'This installer has an invalid release version.'; Exit; end;
  if RegQueryStringValue(HKCU, '{#ProductRegistry}', 'ReleaseVersion', Existing) then begin
    if not ValidRelease(Existing) then Result := 'The installed release version cannot be verified. Uninstall the existing application before installing this release. Your browser data will be kept.'
    else if CompareRelease(Existing, '{#ReleaseVersion}') > 0 then
      Result := 'A newer Xenon release is already installed. Downgrades are not supported. Your installed application has not been changed.';
  end;
end;

function FileAvailable(Name: String): Boolean;
var Handle: THandle;
begin
  Result := True;
  if not FileExists(Name) then Exit;
  // Open without truncating or writing. Loaded executables and exclusive handles
  // fail this check before any package files are replaced.
  Handle := OpenFileForUpdate(Name, $C0000000, 0, 0, 3, $80, 0);
  Result := Handle <> THandle(-1);
  if Result then ReleaseFileHandle(Handle);
end;

function LockedRuntime: Boolean;
begin
  Result := not FileAvailable(ExpandConstant('{app}\Xenon.exe')) or
    not FileAvailable(ExpandConstant('{app}\Xenon.dll')) or
    not FileAvailable(ExpandConstant('{app}\libcef.dll')) or
    not FileAvailable(ExpandConstant('{app}\runtime\node.exe'));
end;

function ForbiddenCommandLine: Boolean;
var I: Integer; Arg: String;
begin
  Result := False;
  for I := 1 to ParamCount do begin
    Arg := Uppercase(ParamStr(I));
    if (Arg = '/CLOSEAPPLICATIONS') or (Arg = '/FORCECLOSEAPPLICATIONS') or
       (Arg = '/RESTARTAPPLICATIONS') then Result := True;
  end;
end;

function InitializeSetup: Boolean;
var Error: String;
begin
  Error := VersionError;
  if ForbiddenCommandLine then Error := 'Xenon Setup does not close or restart applications. Close Xenon and its MCP adapter yourself, then run Setup without application-closing options.';
  Result := Error = '';
  if not Result then SuppressibleMsgBox(Error, mbError, MB_OK, IDOK);
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
  NeedsRestart := False;
  Result := VersionError;
  if Result <> '' then Exit;
  if CompareText(RemoveBackslashUnlessRoot(ExpandConstant('{app}')),
      RemoveBackslashUnlessRoot(ExpandConstant('{#ProductDir}'))) <> 0 then begin
    Result := 'Xenon uses a fixed per-user installation folder so updates and MCP paths remain stable. Run Setup without a /DIR override.';
    Exit;
  end;
  if CheckForMutexes('{#RunningMutex}') then Result := 'Close every Xenon window before installing. Your work will not be closed automatically.'
  else if LockedRuntime then Result := 'Xenon runtime files are in use or cannot be updated. Close Xenon and stop its MCP adapter in your agent client, then retry. No application will be forcibly closed.';
end;

function InitializeUninstall: Boolean;
begin
  // SetupMutex belongs to Setup, not Uninstall. Publish the same marker before
  // checking files so a newly started browser also refuses during uninstall.
  // Keep the handle until process termination, including cancellation paths.
  UninstallMutex := CreateInstallerMutex(0, False, '{#InstallerMutex}');
  if (UninstallMutex = 0) or (LastWindowsError = 183) then begin
    Result := False;
    SuppressibleMsgBox('Another Xenon install or uninstall is running. Close it before trying again.', mbError, MB_OK, IDOK);
    Exit;
  end;
  Result := not LockedRuntime;
  if not Result then SuppressibleMsgBox('Close Xenon and stop its MCP adapter before uninstalling. Your browser profiles and saved accounts will be kept.', mbError, MB_OK, IDOK);
end;

procedure InitializeWizard;
begin
  WizardForm.WelcomeLabel2.Caption := 'Install Xenon for this Windows user. This is an unsigned alpha release.' + #13#10 + #13#10 +
    'Close Xenon and stop its MCP adapter before installing or upgrading. Setup will not close applications or restart Windows.' + #13#10 + #13#10 +
    'Browser profiles, saved accounts, and pairing permissions remain in your separate local application data folder. Uninstalling keeps that data.';
end;
