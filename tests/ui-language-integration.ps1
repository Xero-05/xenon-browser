# Native message-driver regression. It operates only the process it launches,
# using a generated empty profile. This is not physical-input or visual coverage.
param()
$ErrorActionPreference = 'Stop'
$taskRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$taskRun = 'ui-language-' + [Guid]::NewGuid().ToString('N')
$taskProfile = Join-Path $taskRoot ".cache/$taskRun"
$taskBinary = Join-Path $taskRoot 'build/app/Release/Xenon.exe'
$taskDll = Join-Path $taskRoot 'build/app/Release/Xenon.dll'
$taskResults = [Collections.Generic.List[object]]::new()
$taskProcess = $null
$taskShell = [IntPtr]::Zero
$taskHash = (Get-FileHash -LiteralPath $taskDll -Algorithm SHA256).Hash
New-Item -ItemType Directory -Path $taskProfile | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $taskRoot 'out') | Out-Null
Set-Content -LiteralPath (Join-Path $taskProfile 'SYNTHETIC_TEST_PROFILE') -Value 'XENON_SYNTHETIC_LANGUAGE_FIXTURE'
Set-Content -LiteralPath (Join-Path $taskProfile 'ui-settings.json') -Value '{"version":1,"theme":"dark","sidebarWidth":320}'

Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;
public static class XenonLanguageFixture {
  delegate bool EnumProc(IntPtr window, IntPtr data);
  [DllImport("user32.dll")] static extern bool EnumWindows(EnumProc proc, IntPtr data);
  [DllImport("user32.dll")] static extern bool EnumChildWindows(IntPtr window, EnumProc proc, IntPtr data);
  [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr window, out uint pid);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] static extern int GetClassName(IntPtr window, StringBuilder name, int size);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] static extern int GetWindowText(IntPtr window, StringBuilder text, int size);
  [DllImport("user32.dll")] static extern int GetDlgCtrlID(IntPtr window);
  [DllImport("user32.dll")] static extern bool PostMessage(IntPtr window, uint message, IntPtr wp, IntPtr lp);
  static string Class(IntPtr window) {var s=new StringBuilder(128);GetClassName(window,s,s.Capacity);return s.ToString();}
  public static string Text(IntPtr window) {var s=new StringBuilder(4096);GetWindowText(window,s,s.Capacity);return s.ToString();}
  public static IntPtr Find(int pid, string kind, string caption) {
    IntPtr result=IntPtr.Zero;
    EnumWindows((window,data)=>{uint owner;GetWindowThreadProcessId(window,out owner);
      if(owner==pid && Class(window)==kind && (String.IsNullOrEmpty(caption) || Text(window)==caption)){result=window;return false;}return true;},IntPtr.Zero);
    return result;
  }
  public static string[] Captions(IntPtr window) {
    var values=new List<string>();EnumChildWindows(window,(child,data)=>{values.Add(Text(child));return true;},IntPtr.Zero);return values.ToArray();
  }
  public static int Button(IntPtr window,string caption) {
    int result=-1;EnumChildWindows(window,(child,data)=>{if(Class(child)=="Button" && Text(child)==caption){result=GetDlgCtrlID(child);return false;}return true;},IntPtr.Zero);return result;
  }
  public static void Command(IntPtr window,int id) {if(!PostMessage(window,0x111,new IntPtr(id),IntPtr.Zero))throw new Exception("Fixture command failed");}
  public static void AcceptNotice(IntPtr window) {
    IntPtr button=IntPtr.Zero;
    EnumChildWindows(window,(child,data)=>{if(Class(child)=="Button"){button=child;return false;}return true;},IntPtr.Zero);
    if(button==IntPtr.Zero || !PostMessage(window,0x111,new IntPtr(GetDlgCtrlID(button)),button))throw new Exception("Fixture notice dismissal failed");
  }
  public static void Close(IntPtr window) {if(window!=IntPtr.Zero)PostMessage(window,0x10,IntPtr.Zero,IntPtr.Zero);}
}
'@
function Wait-Until([scriptblock]$Action, [string]$Description) {
  $deadline = [DateTime]::UtcNow.AddSeconds(40)
  do {
    $value = & $Action
    if ($value) { return $value }
    if ($taskProcess -and $taskProcess.HasExited) { throw "Fixture exited while waiting for $Description" }
    Start-Sleep -Milliseconds 100
  } while ([DateTime]::UtcNow -lt $deadline)
  throw "Timed out waiting for $Description"
}
function Require([bool]$Condition, [string]$Message) { if (-not $Condition) { throw $Message } }
function Record([string]$Name) {
  $taskResults.Add(@{name=$Name;passed=$true})
  Write-Host "PASS $Name"
}
function Launch-Fixture {
  $profileArgument='--user-data-dir="' + $taskProfile + '"'
  $script:taskProcess = Start-Process -FilePath $taskBinary -ArgumentList @($profileArgument,"--broker-pipe=xenon-$taskRun") -WindowStyle Hidden -PassThru
  Write-Host "Fixture PID $($taskProcess.Id): $taskRun"
  $script:taskShell = Wait-Until { $w=[XenonLanguageFixture]::Find($taskProcess.Id,'XenonBrowserShell',$null); if ($w -ne [IntPtr]::Zero) {$w} } 'fixture shell'
  # Wait for the initial blank page to finish opening before testing commands.
  Wait-Until { [XenonLanguageFixture]::Text($taskShell) -match ' — Xenon$' } 'initial blank tab' | Out-Null
}
function Exit-Fixture {
  [XenonLanguageFixture]::Close($taskShell)
  Require ($taskProcess.WaitForExit(30000)) 'Fixture did not close normally'
  Require ($taskProcess.ExitCode -eq 0) 'Fixture exit code must be zero'
  $script:taskProcess = $null
  $script:taskShell = [IntPtr]::Zero
}
function Set-FixtureLanguage([int]$Command, [string]$Tag) {
  [XenonLanguageFixture]::Command($taskShell,$Command)
  Wait-Until { (Get-Content -LiteralPath (Join-Path $taskProfile 'ui-settings.json') -Raw | ConvertFrom-Json).language -eq $Tag } 'saved language' | Out-Null
  $prompt=Wait-Until { $w=[XenonLanguageFixture]::Find($taskProcess.Id,'#32770','Language / 语言'); if ($w -ne [IntPtr]::Zero) {$w} } 'restart notice'
  [XenonLanguageFixture]::AcceptNotice($prompt)
  Wait-Until { [XenonLanguageFixture]::Find($taskProcess.Id,'#32770','Language / 语言') -eq [IntPtr]::Zero } 'restart notice dismissal' | Out-Null
  $settings=Get-Content -LiteralPath (Join-Path $taskProfile 'ui-settings.json') -Raw | ConvertFrom-Json
  Require ($settings.theme -eq 'dark' -and $settings.sidebarWidth -eq 320) 'Language save must preserve theme and sidebar width'
}
try {
  Launch-Fixture
  Require ([XenonLanguageFixture]::Captions($taskShell) -contains 'Back') 'Existing profiles default to English'
  Set-FixtureLanguage 2201 'zh-CN'
  Require ([XenonLanguageFixture]::Captions($taskShell) -contains 'Back') 'Current process keeps its active language until restart'
  Exit-Fixture
  Record 'English preference switches to zh-CN on the next normal launch'
  Launch-Fixture
  $captions=[XenonLanguageFixture]::Captions($taskShell)
  foreach ($caption in @('后退','前进','刷新','打开地址','控制中心','菜单','新标签页','上一个匹配','下一个匹配','关闭查找')) {
    Require ($captions -contains $caption) "Missing Chinese shell caption: $caption"
  }
  Require ([XenonLanguageFixture]::Text($taskShell) -eq '新标签页 — Xenon') 'Chinese blank-tab title'
  Wait-Until {
    $renderers=Get-CimInstance Win32_Process -Filter "ParentProcessId = $($taskProcess.Id)" | Where-Object {$_.CommandLine -like '*--type=renderer*'}
    @($renderers | Where-Object {$_.CommandLine -like '*--lang=zh-CN*'}).Count -gt 0
  } 'Chinese CEF renderer locale' | Out-Null
  Record 'Real shell HWNDs, blank-tab title and CEF locale use Simplified Chinese'
  [XenonLanguageFixture]::Command($taskShell,2006)
  $controls=Wait-Until { $w=[XenonLanguageFixture]::Find($taskProcess.Id,'XenonControlCenter','Xenon 控制中心');if ($w -ne [IntPtr]::Zero) {$w} } 'Chinese Controls'
  $captions=[XenonLanguageFixture]::Captions($controls)
  foreach ($caption in @('客户端','工作区','密码','创建工作区','配置','接管控制权','交给代理','导入 CSV','保存账号','用户名','密码','检查更新')) {
    Require ($captions -contains $caption) "Missing Chinese Controls caption: $caption"
  }
  Record 'Real Controls sections, ownership and saved-account buttons are translated'
  [XenonLanguageFixture]::Command($controls,[XenonLanguageFixture]::Button($controls,'创建工作区'))
  $configuration=Wait-Until { $w=[XenonLanguageFixture]::Find($taskProcess.Id,'XenonConfiguration','创建工作区');if ($w -ne [IntPtr]::Zero) {$w} } 'workspace configuration'
  Require ([XenonLanguageFixture]::Captions($configuration) -contains '工作区名称') 'Chinese workspace configuration caption'
  [XenonLanguageFixture]::Close($configuration)
  [XenonLanguageFixture]::Command($controls,[XenonLanguageFixture]::Button($controls,'文件'))
  $files=Wait-Until { $w=[XenonLanguageFixture]::Find($taskProcess.Id,'XenonFilePermissions','Xenon 文件权限 — native-default');if ($w -ne [IntPtr]::Zero) {$w} } 'file permissions'
  Require ([XenonLanguageFixture]::Captions($files) -contains '授权文件夹') 'Chinese file permission caption'
  [XenonLanguageFixture]::Close($files)
  Record 'Workspace configuration and file-permission windows are translated'
  [XenonLanguageFixture]::Command($taskShell,2114)
  $about=Wait-Until { $w=[XenonLanguageFixture]::Find($taskProcess.Id,'XenonBrowserPanel','关于 Xenon');if ($w -ne [IntPtr]::Zero) {$w} } 'About panel'
  Require ([XenonLanguageFixture]::Captions($about) -contains '关闭') 'Chinese panel close caption'
  [XenonLanguageFixture]::Close($about)
  Record 'Native browser panels are translated'
  Set-FixtureLanguage 2200 'en-US'
  Exit-Fixture
  Launch-Fixture
  Require ([XenonLanguageFixture]::Captions($taskShell) -contains 'Back') 'English restored after switching back'
  Require ([XenonLanguageFixture]::Text($taskShell) -eq 'New tab — Xenon') 'English blank-tab title restored'
  Exit-Fixture
  Record 'Switching back restores English after restart'
} catch {
  $taskResults.Add(@{name='Language integration';passed=$false;error=$_.Exception.Message})
  Write-Host "FAIL $($_.Exception.Message)"
} finally {
  if ($taskProcess -and -not $taskProcess.HasExited) {
    try {
      if ($taskShell -eq [IntPtr]::Zero) {$taskShell=[XenonLanguageFixture]::Find($taskProcess.Id,'XenonBrowserShell',$null)}
      $prompt=[XenonLanguageFixture]::Find($taskProcess.Id,'#32770','Language / 语言')
      if ($prompt -ne [IntPtr]::Zero) {[XenonLanguageFixture]::AcceptNotice($prompt)}
      [XenonLanguageFixture]::Close($taskShell)
      if (-not $taskProcess.WaitForExit(30000)) {throw 'Fixture could not close normally'}
    } catch {$taskResults.Add(@{name='Fixture cleanup';passed=$false;error=$_.Exception.Message})}
  }
  $taskHashAtEnd=(Get-FileHash -LiteralPath $taskDll -Algorithm SHA256).Hash
  if ($taskHashAtEnd -ne $taskHash) {$taskResults.Add(@{name='Binary stability';passed=$false;error='Application DLL changed during the test'})}
  $passed=$taskResults.Count -eq 6 -and @($taskResults | Where-Object {-not $_.passed}).Count -eq 0
  $report=@{run=$taskRun;capturedAt=[DateTime]::UtcNow.ToString('o');binary='build/app/Release/Xenon.exe';applicationDllSha256=$taskHash;applicationDllSha256AtEnd=$taskHashAtEnd;profile=$taskProfile;coverage='native message driver, no physical input or visual acceptance';passed=$passed;results=$taskResults}
  $report | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $taskRoot "out/$taskRun.json") -Encoding utf8
  Write-Host "Report: out/$taskRun.json"
}
if (-not $passed) {exit 1}
