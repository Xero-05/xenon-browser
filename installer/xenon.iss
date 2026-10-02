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
  #define ProductUninstallRegistry "Software\Microsoft\Windows\CurrentVersion\Uninstall\Xenon.InstallerFixture." + TestId + "_is1"
  #ifndef TestProfileDir
    #error TestProfileDir is required for isolated installer tests
  #endif
  #define ProfileDir TestProfileDir
  #define RunningMutex "Local\XenonInstallerFixtureRunning-" + TestId
  #define InstallerMutex "Local\XenonInstallerFixtureSetup-" + TestId
  #define OutputName "fixture-" + ReleaseVersion
#else
  #define ProductId "{{C0D7CF52-A672-4B76-82E4-BE1664F9D305}"
  #define ProductName "Xenon Browser"
  #define ProductDir "{localappdata}\Programs\Xenon Browser"
  #define ProductRegistry "Software\Xenon Browser\Installer"
  #define ProductUninstallRegistry "Software\Microsoft\Windows\CurrentVersion\Uninstall\{C0D7CF52-A672-4B76-82E4-BE1664F9D305}_is1"
  #define ProfileDir "{localappdata}\Xenon Browser"
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
DisableDirPage=no
AlwaysShowDirOnReadyPage=yes
DisableProgramGroupPage=yes
UsePreviousAppDir=yes
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
function PathAttributes(Name: String): DWORD;
  external 'GetFileAttributesW@kernel32.dll stdcall';
function LocalDriveType(Name: String): UINT;
  external 'GetDriveTypeW@kernel32.dll stdcall';
function LongPath(Name: String; Buffer: String; BufferLength: DWORD): DWORD;
  external 'GetLongPathNameW@kernel32.dll stdcall';

var UninstallMutex: THandle;

function NormalizedDir(Name: String): String;
var Existing, Tail, Buffer: String; Count: DWORD;
begin
  Result := RemoveBackslashUnlessRoot(ExpandFileName(Name));
  // Resolve existing 8.3 aliases without creating the selected directory.
  Existing := Result; Tail := '';
  while (not DirExists(Existing)) and (Length(Existing) > 3) do begin
    Tail := '\' + ExtractFileName(Existing) + Tail;
    Existing := RemoveBackslashUnlessRoot(ExtractFileDir(Existing));
  end;
  Buffer := StringOfChar(#0, 32768);
  Count := LongPath(Existing, Buffer, Length(Buffer));
  if (Count > 0) and (Count < DWORD(Length(Buffer))) then begin
    Result := RemoveBackslashUnlessRoot(Copy(Buffer, 1, Count));
    if Tail <> '' then Result := AddBackslash(Result) + Copy(Tail, 2, Length(Tail));
  end;
end;

function RegisteredInstallDir: String;
begin
  Result := '';
  // The alpha.10 installer already wrote this standard Inno value. Use exactly
  // the same value as UsePreviousAppDir; do not infer a path from today's default.
  RegQueryStringValue(HKCU, '{#ProductUninstallRegistry}', 'Inno Setup: App Path', Result);
  if Result <> '' then Result := NormalizedDir(Result);
end;

function PathWithin(Path, Parent: String): Boolean;
begin
  Result := (CompareText(Path, Parent) = 0) or
    (CompareText(Copy(Path, 1, Length(AddBackslash(Parent))), AddBackslash(Parent)) = 0);
end;

function DestinationError(Name: String): String;
var Selected, Existing, Profile, Ancestor, Parent: String;
    Attributes, EnumerationError: DWORD; Found: TFindRec;
begin
  Result := '';
  Selected := NormalizedDir(Name);
  if (Length(Selected) <= 3) or (Selected[2] <> ':') or (Selected[3] <> '\') or
     (LocalDriveType(Copy(Selected, 1, 3)) <> 3) then begin
    Result := 'Choose a dedicated folder on a local fixed drive, not a drive root or network location.';
    Exit;
  end;
  Profile := NormalizedDir(ExpandConstant('{#ProfileDir}'));
  if PathWithin(Selected, Profile) or PathWithin(Profile, Selected) then begin
    Result := 'Choose an application folder separate from Xenon browser data. Do not select the profile folder, a folder inside it, or a folder containing it.';
    Exit;
  end;
  // Do not follow a junction/symlink into profile or unrelated application data.
  Ancestor := Selected;
  while Length(Ancestor) > 3 do begin
    Attributes := PathAttributes(Ancestor);
    if (Attributes <> $FFFFFFFF) and ((Attributes and $400) <> 0) then begin
      Result := 'Choose a normal folder without symbolic links or directory junctions in its path.';
      Exit;
    end;
    Parent := RemoveBackslashUnlessRoot(ExtractFileDir(Ancestor));
    if Parent = Ancestor then Break;
    Ancestor := Parent;
  end;
  Existing := RegisteredInstallDir;
  if Existing <> '' then begin
    if CompareText(Selected, Existing) <> 0 then
      Result := 'Updates keep the existing Xenon installation folder so shortcuts and MCP paths remain valid. To move Xenon, uninstall it first, then reinstall and choose the new folder. Uninstalling keeps browser data; update your MCP client paths after moving.';
    Exit;
  end;
  if RegKeyExists(HKCU, '{#ProductUninstallRegistry}') then begin
    Result := 'The previous installation folder cannot be verified. Uninstall the existing Xenon installation before choosing a new location. Your browser data will be kept.';
    Exit;
  end;
  Attributes := PathAttributes(Selected);
  if Attributes = $FFFFFFFF then begin
    EnumerationError := LastWindowsError;
    if (EnumerationError <> 2) and (EnumerationError <> 3) then
      Result := 'Setup cannot verify the selected folder is empty. Choose a new folder that your Windows user can read and write.';
    Exit;
  end;
  if (Attributes and $10) = 0 then begin
    Result := 'The selected location is a file. Choose a new, empty application folder.';
    Exit;
  end;
  if FindFirst(AddBackslash(Selected) + '*', Found) then begin
    try
      repeat
        if (Found.Name <> '.') and (Found.Name <> '..') then begin
          Result := 'Choose a new or empty folder dedicated to Xenon. Setup will not overwrite an unrelated folder or a portable copy; choose another folder instead.';
          Break;
        end;
      until not FindNext(Found);
      EnumerationError := LastWindowsError;
      if (Result = '') and (EnumerationError <> 18) then
        Result := 'Setup cannot finish checking the selected folder is empty. Choose a new folder that your Windows user can read and write.';
    finally FindClose(Found); end;
  end else Result := 'Setup cannot verify the selected folder is empty. Choose a new folder that your Windows user can read and write.';
end;

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
  Result := DestinationError(ExpandConstant('{app}'));
  if Result <> '' then Exit;
  if CheckForMutexes('{#RunningMutex}') then Result := 'Close every Xenon window before installing. Your work will not be closed automatically.'
  else if LockedRuntime then Result := 'Xenon runtime files are in use or cannot be updated. Close Xenon and stop its MCP adapter in your agent client, then retry. No application will be forcibly closed.';
end;

function ShouldSkipPage(PageID: Integer): Boolean;
begin
  Result := False;
  if PageID = wpSelectDir then begin
    Result := RegisteredInstallDir <> '';
    if Result then Log('Xenon destination page: existing installation retained.')
    else Log('Xenon destination page: enabled for new installation.');
  end;
end;

function NextButtonClick(CurPageID: Integer): Boolean;
var Error: String;
begin
  Result := True;
  if CurPageID = wpSelectDir then begin
    Error := DestinationError(WizardDirValue);
    Result := Error = '';
    if not Result then SuppressibleMsgBox(Error, mbError, MB_OK, IDOK);
  end;
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
    'Choose a new or empty application folder. Updates retain the registered installation folder; to move it, uninstall and reinstall, then update your MCP client paths.' + #13#10 + #13#10 +
    'Browser profiles, saved accounts, and pairing permissions remain in your separate local application data folder. Uninstalling keeps that data.';
end;
