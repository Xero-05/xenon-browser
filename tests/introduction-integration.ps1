# Disposable production-browser regression. Native messages drive only this
# script's process; window captures contain synthetic introduction UI only.
# This does not establish physical-input or multi-monitor/DPI acceptance.
param()
$ErrorActionPreference = 'Stop'
$taskRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$taskRun = 'introduction-' + [Guid]::NewGuid().ToString('N')
$taskProfiles = Join-Path $taskRoot ".cache/$taskRun"
$taskProfile = Join-Path $taskProfiles 'tour'
$taskOutput = Join-Path $taskRoot "out/$taskRun"
$taskBinary = Join-Path $taskRoot 'build/app/Release/Xenon.exe'
$taskDll = Join-Path $taskRoot 'build/app/Release/Xenon.dll'
$taskHash = (Get-FileHash -LiteralPath $taskDll -Algorithm SHA256).Hash
$taskResults = [Collections.Generic.List[object]]::new()
$taskProcess = $null
$taskShell = [IntPtr]::Zero
$taskTour = [IntPtr]::Zero
$taskDocs = 'https://github.com/Xero-05/xenon-browser/blob/main/docs/GETTING_STARTED.md'
New-Item -ItemType Directory -Path $taskProfile -Force | Out-Null
New-Item -ItemType Directory -Path $taskOutput -Force | Out-Null

Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;
public static class XenonIntroductionFixture {
  delegate bool EnumProc(IntPtr window, IntPtr data);
  [DllImport("user32.dll")] static extern bool EnumWindows(EnumProc proc, IntPtr data);
  [DllImport("user32.dll")] static extern bool EnumChildWindows(IntPtr window, EnumProc proc, IntPtr data);
  [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr window, out uint pid);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] static extern int GetClassName(IntPtr window, StringBuilder name, int size);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] static extern int GetWindowText(IntPtr window, StringBuilder text, int size);
  [DllImport("user32.dll")] static extern IntPtr GetDlgItem(IntPtr window, int id);
  [DllImport("user32.dll")] static extern bool PostMessage(IntPtr window, uint message, IntPtr wp, IntPtr lp);
  [DllImport("user32.dll")] static extern IntPtr SendMessage(IntPtr window, uint message, IntPtr wp, IntPtr lp);
  [DllImport("user32.dll",CharSet=CharSet.Unicode,EntryPoint="SendMessageW")] static extern IntPtr ReadText(IntPtr window,uint message,IntPtr wp,StringBuilder text);
  [DllImport("user32.dll")] static extern bool IsWindowVisible(IntPtr window);
  [DllImport("user32.dll")] static extern bool IsWindowEnabled(IntPtr window);
  public struct Rect {public int Left,Top,Right,Bottom;}
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr window,out Rect rect);
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr window,IntPtr dc,uint flags);
  public static string Text(IntPtr window) {var text=new StringBuilder(4096);GetWindowText(window,text,text.Capacity);return text.ToString();}
  static string Class(IntPtr window) {var text=new StringBuilder(128);GetClassName(window,text,text.Capacity);return text.ToString();}
  public static IntPtr Find(int pid,string kind) {
    IntPtr result=IntPtr.Zero;EnumWindows((window,data)=>{uint owner;GetWindowThreadProcessId(window,out owner);
      if(owner==pid && Class(window)==kind){result=window;return false;}return true;},IntPtr.Zero);return result;
  }
  public static string[] Captions(IntPtr window) {
    var values=new List<string>();EnumChildWindows(window,(child,data)=>{if(IsWindowVisible(child))values.Add(Text(child));return true;},IntPtr.Zero);return values.ToArray();
  }
  public static string Caption(IntPtr window,int id) {
    var child=GetDlgItem(window,id);
    // GetWindowText cannot read an Edit control in another process. WM_GETTEXT
    // is a system-marshaled message and reads the fixture's actual address.
    if(Class(child)=="Edit"){var text=new StringBuilder(4096);ReadText(child,0xD,new IntPtr(text.Capacity),text);return text.ToString();}
    return Text(child);
  }
  public static bool Enabled(IntPtr window) {return IsWindowEnabled(window);}
  public static void Command(IntPtr window,int id) {if(!PostMessage(window,0x111,new IntPtr(id),IntPtr.Zero))throw new Exception("Fixture command failed");}
  public static void Language(IntPtr window,int index) {
    SendMessage(GetDlgItem(window,4),0x14E,new IntPtr(index),IntPtr.Zero);
    PostMessage(window,0x111,new IntPtr(4 | (1<<16)),GetDlgItem(window,4));
  }
  public static void Close(IntPtr window) {if(window!=IntPtr.Zero)PostMessage(window,0x10,IntPtr.Zero,IntPtr.Zero);}
}
'@
Add-Type -AssemblyName System.Drawing
function Require([bool]$Condition,[string]$Message) {if(-not $Condition){throw $Message}}
function Wait-Until([scriptblock]$Action,[string]$Description) {
  $deadline=[DateTime]::UtcNow.AddSeconds(40)
  do {$value=& $Action;if($value){return $value};if($taskProcess -and $taskProcess.HasExited){throw "Fixture exited while waiting for $Description"};Start-Sleep -Milliseconds 100} while([DateTime]::UtcNow -lt $deadline)
  throw "Timed out waiting for $Description"
}
function Record([string]$Name) {$taskResults.Add(@{name=$Name;passed=$true});Write-Host "PASS $Name"}
function Launch-Fixture {
  $script:taskShell=[IntPtr]::Zero;$script:taskTour=[IntPtr]::Zero
  $profileArgument='--user-data-dir="'+$taskProfile+'"'
  $script:taskProcess=Start-Process -FilePath $taskBinary -ArgumentList @($profileArgument,"--broker-pipe=xenon-$taskRun") -WindowStyle Hidden -PassThru
  Write-Host "Fixture PID $($taskProcess.Id)"
}
function Find-Tour {$script:taskTour=Wait-Until {$w=[XenonIntroductionFixture]::Find($taskProcess.Id,'XenonIntroduction');if($w -ne [IntPtr]::Zero){$w}} 'introduction window'}
function Find-Shell {
  $script:taskShell=Wait-Until {$w=[XenonIntroductionFixture]::Find($taskProcess.Id,'XenonBrowserShell');if($w -ne [IntPtr]::Zero){$w}} 'browser shell'
  Wait-Until {[XenonIntroductionFixture]::Text($taskShell) -match ' — Xenon$'} 'initial page' | Out-Null
}
function Exit-Fixture {
  [XenonIntroductionFixture]::Close($taskShell)
  Require ($taskProcess.WaitForExit(30000)) 'Fixture must exit normally'
  Require ($taskProcess.ExitCode -eq 0) 'Fixture exit code must be zero'
  $script:taskProcess=$null;$script:taskShell=[IntPtr]::Zero;$script:taskTour=[IntPtr]::Zero
}
function Page([int]$Command,[string]$Title) {
  [XenonIntroductionFixture]::Command($taskTour,$Command)
  Wait-Until {[XenonIntroductionFixture]::Caption($taskTour,10) -eq $Title} $Title | Out-Null
}
function Capture([string]$Name) {
  # PrintWindow targets just the generated fixture's introduction, never the desktop.
  Start-Sleep -Milliseconds 200
  $rect=[XenonIntroductionFixture+Rect]::new()
  Require ([XenonIntroductionFixture]::GetWindowRect($taskTour,[ref]$rect)) 'Capture fixture bounds'
  $bitmap=[Drawing.Bitmap]::new($rect.Right-$rect.Left,$rect.Bottom-$rect.Top)
  $graphics=[Drawing.Graphics]::FromImage($bitmap);$dc=$graphics.GetHdc()
  try {Require ([XenonIntroductionFixture]::PrintWindow($taskTour,$dc,0)) 'Capture fixture introduction'} finally {$graphics.ReleaseHdc($dc);$graphics.Dispose()}
  try {$bitmap.Save((Join-Path $taskOutput "$Name.png"),[Drawing.Imaging.ImageFormat]::Png)} finally {$bitmap.Dispose()}
}
try {
  Launch-Fixture;Find-Tour
  Require ([XenonIntroductionFixture]::Caption($taskTour,10) -eq 'Welcome to Xenon') 'First step is language selection'
  Require ([XenonIntroductionFixture]::Find($taskProcess.Id,'XenonBrowserShell') -eq [IntPtr]::Zero) 'CEF must wait for language selection'
  Capture 'english-language'
  $profileArgument='--user-data-dir="'+$taskProfile+'"'
  $duplicate=Start-Process -FilePath $taskBinary -ArgumentList @($profileArgument,"--broker-pipe=xenon-$taskRun") -WindowStyle Hidden -PassThru
  Require ($duplicate.WaitForExit(10000) -and $duplicate.ExitCode -eq 0) 'Duplicate startup exits without racing CEF or the introduction'
  [XenonIntroductionFixture]::Close($taskTour)
  Require ($taskProcess.WaitForExit(10000) -and $taskProcess.ExitCode -eq 0) 'Closing setup cancels startup normally'
  Require (-not (Test-Path -LiteralPath (Join-Path $taskProfile 'ui-settings.json'))) 'Canceling does not mark setup complete'
  $taskProcess=$null
  Record 'Fresh language selection, duplicate-launch gate and cancel/retry'

  Launch-Fixture;Find-Tour
  [XenonIntroductionFixture]::Language($taskTour,1)
  Wait-Until {[XenonIntroductionFixture]::Caption($taskTour,10) -eq '欢迎使用 Xenon'} 'Chinese language preview' | Out-Null
  Capture 'chinese-language'
  Page 1 '快速了解浏览器';Capture 'chinese-browser'
  Page 3 '欢迎使用 Xenon'
  Page 1 '快速了解浏览器';Page 1 '组织工作，掌握控制权';Capture 'chinese-controls'
  Page 1 '开始浏览吧';Capture 'chinese-ready'
  Require ([XenonIntroductionFixture]::Captions($taskTour) -contains 'GitHub 使用文档 ↗') 'Final step has the documentation link'
  [XenonIntroductionFixture]::Command($taskTour,1);Find-Shell
  Wait-Until {[XenonIntroductionFixture]::Text($taskShell) -eq '新标签页 — Xenon'} ('Chinese blank tab; current title: '+[XenonIntroductionFixture]::Text($taskShell)) | Out-Null
  Require ([XenonIntroductionFixture]::Captions($taskShell) -contains '后退') 'Chosen language applies on the very first browser launch'
  $settings=Get-Content -LiteralPath (Join-Path $taskProfile 'ui-settings.json') -Raw | ConvertFrom-Json
  Require ($settings.language -eq 'zh-CN' -and $settings.introductionCompleted) 'Language and completion are saved together'
  Wait-Until {@(Get-CimInstance Win32_Process -Filter "ParentProcessId = $($taskProcess.Id)" | Where-Object {$_.CommandLine -like '*--type=renderer*' -and $_.CommandLine -like '*--lang=zh-CN*'}).Count -gt 0} 'Chinese CEF renderer' | Out-Null
  Record 'Translated tour, Previous/Next, saved completion and immediate CEF locale'
  Exit-Fixture;Launch-Fixture;Find-Shell
  Require ([XenonIntroductionFixture]::Find($taskProcess.Id,'XenonIntroduction') -eq [IntPtr]::Zero) 'Completed profiles skip setup on restart'
  Record 'Restart preserves language and opens a blank tab without setup'

  $before=Get-Content -LiteralPath (Join-Path $taskProfile 'ui-settings.json') -Raw
  [XenonIntroductionFixture]::Command($taskShell,2123);Find-Tour
  Require ([XenonIntroductionFixture]::Caption($taskTour,10) -eq '快速了解浏览器') 'Menu reopens the quick tour'
  Require (-not [XenonIntroductionFixture]::Enabled($taskShell)) 'Tour supports native modal navigation'
  Page 1 '组织工作，掌握控制权';Page 1 '开始浏览吧'
  [XenonIntroductionFixture]::Command($taskTour,5)
  Wait-Until {[XenonIntroductionFixture]::Caption($taskShell,2004) -eq $taskDocs} 'documentation in a new native browser tab' | Out-Null
  Require ([XenonIntroductionFixture]::Enabled($taskShell)) 'Browser is enabled after the tour'
  Require ((Get-Content -LiteralPath (Join-Path $taskProfile 'ui-settings.json') -Raw) -eq $before) 'Replaying the tour does not change preferences'
  [XenonIntroductionFixture]::Command($taskShell,2118)
  Wait-Until {[XenonIntroductionFixture]::Text($taskShell) -eq '新标签页 — Xenon'} 'original blank tab survives documentation' | Out-Null
  [XenonIntroductionFixture]::Command($taskShell,2124)
  Wait-Until {[XenonIntroductionFixture]::Caption($taskShell,2004) -eq $taskDocs} 'direct Menu documentation link' | Out-Null
  Record 'Menu tour reopens, preserves settings and opens the GitHub URL'
  Exit-Fixture

  $taskProfile=Join-Path $taskProfiles 'skip';Launch-Fixture;Find-Tour
  [XenonIntroductionFixture]::Command($taskTour,2);Find-Shell
  Require ([XenonIntroductionFixture]::Text($taskShell) -eq 'New tab — Xenon') 'Skip opens a blank English tab'
  Require ((Get-Content -LiteralPath (Join-Path $taskProfile 'ui-settings.json') -Raw | ConvertFrom-Json).introductionCompleted) 'Skip is durable'
  Exit-Fixture;Record 'Skipping setup saves the selection and starts browsing'

  $taskProfile=Join-Path $taskProfiles 'documentation';Launch-Fixture;Find-Tour
  Page 1 'Your browser, at a glance';Capture 'english-browser'
  Page 1 'Keep work organized and stay in control';Capture 'english-controls'
  Page 1 "You're ready to browse";Capture 'english-ready'
  [XenonIntroductionFixture]::Command($taskTour,5);Find-Shell
  Wait-Until {[XenonIntroductionFixture]::Caption($taskShell,2004) -eq $taskDocs} 'first-run guide navigation' | Out-Null
  Exit-Fixture;Record 'First-run documentation link starts Xenon at the requested guide'
} catch {$taskResults.Add(@{name='Introduction integration';passed=$false;error=$_.Exception.Message});Write-Host "FAIL $($_.Exception.Message)"}
finally {
  if($taskProcess -and -not $taskProcess.HasExited){
    [XenonIntroductionFixture]::Close([XenonIntroductionFixture]::Find($taskProcess.Id,'XenonIntroduction'))
    [XenonIntroductionFixture]::Close([XenonIntroductionFixture]::Find($taskProcess.Id,'XenonBrowserShell'))
    if(-not $taskProcess.WaitForExit(30000)){$taskResults.Add(@{name='Cleanup';passed=$false;error='Fixture did not close normally'})}
  }
  $endHash=(Get-FileHash -LiteralPath $taskDll -Algorithm SHA256).Hash
  if($endHash -ne $taskHash){$taskResults.Add(@{name='Binary stability';passed=$false;error='DLL changed during test'})}
  $passed=$taskResults.Count -eq 6 -and @($taskResults | Where-Object {-not $_.passed}).Count -eq 0
  @{run=$taskRun;capturedAt=[DateTime]::UtcNow.ToString('o');applicationDllSha256=$taskHash;applicationDllSha256AtEnd=$endHash;profile=$taskProfiles;coverage='native message driver and fixture-only window captures; no physical input or multi-monitor/DPI acceptance';passed=$passed;results=$taskResults} | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $taskOutput 'results.json') -Encoding utf8
  Write-Host "Report: out/$taskRun/results.json"
}
if(-not $passed){exit 1}
