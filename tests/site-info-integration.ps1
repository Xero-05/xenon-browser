# Native message-driver regression for the site-information button, bubble,
# site settings and cookie panels. It operates only the processes it launches,
# with generated empty profiles and loopback fixtures: production Xenon.exe over
# HTTP, and the unshipped XenonAuthTest build for its pinned test certificate.
# Windows are shown without activation beyond every monitor. Xenon's thread
# activation and focus are not the desktop foreground; this is not physical
# input or on-screen visual acceptance.
param()
$ErrorActionPreference = 'Stop'
$taskRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$taskRun = 'site-info-' + [Guid]::NewGuid().ToString('N')
$taskBinary = Join-Path $taskRoot 'build/app/Release/Xenon.exe'
$taskDll = Join-Path $taskRoot 'build/app/Release/Xenon.dll'
$taskFixtureBinary = Join-Path $taskRoot 'build/auth-fixture/Release/XenonAuthTest.exe'
$taskResults = [Collections.Generic.List[object]]::new()
$taskHash = (Get-FileHash -LiteralPath $taskDll -Algorithm SHA256).Hash
$taskOut = Join-Path $taskRoot 'out'
New-Item -ItemType Directory -Force -Path $taskOut | Out-Null
Add-Type -AssemblyName System.Drawing, System.Windows.Forms
Add-Type -ReferencedAssemblies System.Drawing -TypeDefinition @'
using System; using System.Collections.Generic; using System.Drawing; using System.Drawing.Imaging; using System.Runtime.InteropServices; using System.Text;
public static class XenonSiteFixture {
  delegate bool EnumProc(IntPtr window, IntPtr data);
  [DllImport("user32.dll")] static extern bool EnumWindows(EnumProc proc, IntPtr data);
  [DllImport("user32.dll")] static extern bool EnumChildWindows(IntPtr window, EnumProc proc, IntPtr data);
  [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr window, out uint pid);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] static extern int GetClassName(IntPtr window, StringBuilder name, int size);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] static extern int GetWindowText(IntPtr window, StringBuilder text, int size);
  [DllImport("user32.dll", CharSet=CharSet.Unicode, EntryPoint="SendMessageW")] public static extern IntPtr WriteText(IntPtr window, uint message, IntPtr wp, string text);
  [DllImport("user32.dll", EntryPoint="SendMessageW")] public static extern IntPtr Send(IntPtr window, uint message, IntPtr wp, IntPtr lp);
  [DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr window, int id);
  [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr window, uint message, IntPtr wp, IntPtr lp);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr window);
  [DllImport("user32.dll")] public static extern bool IsWindowEnabled(IntPtr window);
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr window, int command);
  [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr window, IntPtr after, int x, int y, int cx, int cy, uint flags);
  [DllImport("user32.dll")] public static extern bool SetProcessDpiAwarenessContext(IntPtr value);
  [DllImport("user32.dll")] static extern bool PrintWindow(IntPtr window, IntPtr dc, uint flags);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr window, out RECT rect);
  static string Class(IntPtr window) { var s = new StringBuilder(128); GetClassName(window, s, s.Capacity); return s.ToString(); }
  public static string Text(IntPtr window) { var s = new StringBuilder(4096); GetWindowText(window, s, s.Capacity); return s.ToString(); }
  public static IntPtr Find(int pid, string kind, string caption) {
    IntPtr result = IntPtr.Zero;
    EnumWindows((window, data) => { uint owner; GetWindowThreadProcessId(window, out owner);
      if (owner == pid && Class(window) == kind && IsWindowVisible(window) && (String.IsNullOrEmpty(caption) || Text(window) == caption)) { result = window; return false; } return true; }, IntPtr.Zero);
    return result;
  }
  public static string[] Captions(IntPtr window) { var values = new List<string>(); EnumChildWindows(window, (child, data) => { if (IsWindowVisible(child)) values.Add(Text(child)); return true; }, IntPtr.Zero); return values.ToArray(); }
  public static void Click(IntPtr parent, int id) { var control = GetDlgItem(parent, id); if (control == IntPtr.Zero || !PostMessage(parent, 0x111, new IntPtr(id), control)) throw new Exception("Fixture click failed: " + id); }
  // flags 0 asks windows to print themselves; 2 renders a whole browser window.
  public static void Capture(IntPtr window, string path) { Capture(window, path, 0); }
  // Mean luminance (0-255) of a rendered window, sampled every other pixel.
  public static double Brightness(IntPtr window) {
    RECT rect; GetWindowRect(window, out rect); double total = 0; int count = 0;
    using (var bitmap = new Bitmap(Math.Max(1, rect.Right - rect.Left), Math.Max(1, rect.Bottom - rect.Top))) {
      using (var graphics = Graphics.FromImage(bitmap)) { var dc = graphics.GetHdc(); PrintWindow(window, dc, 2); graphics.ReleaseHdc(dc); }
      for (int y = 0; y < bitmap.Height; y += 2) for (int x = 0; x < bitmap.Width; x += 2) { var c = bitmap.GetPixel(x, y); total += 0.299 * c.R + 0.587 * c.G + 0.114 * c.B; ++count; }
    }
    return count == 0 ? 255 : total / count;
  }
  public static void Capture(IntPtr window, string path, uint flags) {
    RECT rect; GetWindowRect(window, out rect);
    using (var bitmap = new Bitmap(Math.Max(1, rect.Right - rect.Left), Math.Max(1, rect.Bottom - rect.Top))) {
      using (var graphics = Graphics.FromImage(bitmap)) { var dc = graphics.GetHdc(); PrintWindow(window, dc, flags); graphics.ReleaseHdc(dc); }
      bitmap.Save(path, ImageFormat.Png);
    }
  }
}
'@
try { [void][XenonSiteFixture]::SetProcessDpiAwarenessContext([IntPtr](-4)) } catch {}
$WM_SETTEXT = 0x000C; $WM_KEYDOWN = 0x0100; $WM_CLOSE = 0x0010; $WM_COMMAND = 0x0111; $VK_RETURN = 0x0D; $VK_HOME = 0x24
$CB_GETCURSEL = 0x147; $CB_SETCURSEL = 0x14E; $CBN_SELCHANGE = 1; $LVM_GETITEMCOUNT = 0x1004; $LVM_GETSELECTEDCOUNT = 0x1032
# Shell, bubble and panel control IDs from native/src/cef_shell.cpp.
$SiteInfo = 2017; $SiteBack = 2701; $SiteConnection = 2702; $SiteCertificate = 2703; $SiteCookies = 2704; $SiteSettings = 2705; $SiteReset = 2706; $SiteSetting = 2720
$PanelLeft = 21; $PanelRight = 22; $PanelSetting = 200; $PopupsIndex = 4
function Wait-Until([scriptblock]$Action, [string]$Description) {
  $deadline = [DateTime]::UtcNow.AddSeconds(30)
  do {
    $value = & $Action
    if ($value) { return $value }
    if ($script:taskProcess -and $script:taskProcess.HasExited) { throw "Fixture exited while waiting for $Description" }
    Start-Sleep -Milliseconds 100
  } while ([DateTime]::UtcNow -lt $deadline)
  throw "Timed out waiting for $Description"
}
function Require([bool]$Condition, [string]$Message) { if (-not $Condition) { throw $Message } }
function Record([string]$Name) { $taskResults.Add(@{name=$Name;passed=$true}); Write-Host "PASS $Name" }
function Start-Fixture([string]$Binary, [string]$Name, [string]$Theme = 'light') {
  $profile = Join-Path $taskRoot ".cache/$taskRun-$Name"; $script:taskProfiles += $profile
  New-Item -ItemType Directory -Path $profile | Out-Null
  Set-Content -LiteralPath (Join-Path $profile 'SYNTHETIC_TEST_PROFILE') -Value 'XENON_SYNTHETIC_SITE_INFO_FIXTURE'
  [IO.File]::WriteAllText((Join-Path $profile 'ui-settings.json'), "{`"version`":1,`"theme`":`"$Theme`",`"introductionCompleted`":true,`"automaticUpdateChecks`":false}")
  $script:taskProcess = Start-Process -FilePath $Binary -ArgumentList @('--user-data-dir="' + $profile + '"', "--broker-pipe=xenon-$taskRun-$Name") -WindowStyle Minimized -PassThru
  Write-Host "Fixture PID $($taskProcess.Id): $taskRun ($Name)"
  $shell = Wait-Until { $w = [XenonSiteFixture]::Find($taskProcess.Id, 'XenonBrowserShell', $null); if ($w -ne [IntPtr]::Zero) { $w } } 'fixture shell'
  Wait-Until { [XenonSiteFixture]::Text($shell) -match ' — Xenon$' } 'initial blank tab' | Out-Null
  [void][XenonSiteFixture]::ShowWindow($shell, 4)
  [void][XenonSiteFixture]::SetWindowPos($shell, [IntPtr]1, [System.Windows.Forms.SystemInformation]::VirtualScreen.Left - 2400, 120, 1200, 760, 0x0010)
  return $shell
}
function Stop-Fixture($Shell) {
  [void][XenonSiteFixture]::PostMessage($Shell, $WM_CLOSE, [IntPtr]::Zero, [IntPtr]::Zero)
  Require ($taskProcess.WaitForExit(30000)) 'Fixture did not close normally'
  Require ($taskProcess.ExitCode -eq 0) 'Fixture exit code must be zero'
  $script:taskProcess = $null
}
function Open-Page($Shell, [string]$Url, [string]$Title) {
  $address = [XenonSiteFixture]::GetDlgItem($Shell, 2004)
  [void][XenonSiteFixture]::WriteText($address, $WM_SETTEXT, [IntPtr]::Zero, $Url); [void][XenonSiteFixture]::PostMessage($address, $WM_KEYDOWN, [IntPtr]$VK_RETURN, [IntPtr]::Zero)
  Wait-Until { [XenonSiteFixture]::Text($Shell) -eq "$Title — Xenon" } "page $Url" | Out-Null
}
function Open-Bubble($Shell) {
  Wait-Until { [XenonSiteFixture]::IsWindowVisible([XenonSiteFixture]::GetDlgItem($Shell, $SiteInfo)) } 'site-info button' | Out-Null
  Start-Sleep -Milliseconds 400
  [XenonSiteFixture]::Click($Shell, $SiteInfo)
  return Wait-Until { $w = [XenonSiteFixture]::Find($taskProcess.Id, 'XenonSiteInfo', $null); if ($w -ne [IntPtr]::Zero) { $w } } 'site-information bubble'
}
function Wait-Panel([string]$Caption) { Wait-Until { $w = [XenonSiteFixture]::Find($taskProcess.Id, 'XenonBrowserPanel', $Caption); if ($w -ne [IntPtr]::Zero) { $w } } $Caption }
function Combo-Value($Window, [int]$Id) { [int][XenonSiteFixture]::Send([XenonSiteFixture]::GetDlgItem($Window, $Id), $CB_GETCURSEL, [IntPtr]::Zero, [IntPtr]::Zero) }
# The latest fixture request for a path, with the fixture cookies it carried.
function Last-Request([string]$Path) { Get-Content -LiteralPath $taskServerLog | Where-Object { $_ -like "GET $Path COOKIES *" } | Select-Object -Last 1 }

$taskServerLog = Join-Path $taskOut "$taskRun-server.log"
$taskServer = Start-Process -FilePath 'node' -ArgumentList ('"' + (Join-Path $taskRoot 'tests/fixtures/site-info-server.mjs') + '"') -RedirectStandardOutput $taskServerLog -WindowStyle Hidden -PassThru
$taskAuthServer = $null
$taskProcess = $null
$taskProfiles = @()
$expected = 7
try {
  $port = Wait-Until { if (Test-Path $taskServerLog) { $line = Get-Content -LiteralPath $taskServerLog | Where-Object { $_ -like 'PORT *' } | Select-Object -First 1; if ($line) { $line.Split(' ')[1] } } } 'loopback fixture server'
  $base = "http://127.0.0.1:$port"
  # Dark, as dark native controls are the easiest to regress.
  $shell = Start-Fixture $taskBinary 'http' 'dark'
  Open-Page $shell "$base/cookies" 'Site info fixture /cookies'
  Open-Page $shell "$base/check" 'Site info fixture /check'
  Require ((Last-Request '/check') -like '* COOKIES xenon_fixture_a,xenon_fixture_b') 'The fixture page set both synthetic cookies'
  Start-Sleep -Milliseconds 700; [XenonSiteFixture]::Capture($shell, (Join-Path $taskOut "$taskRun-toolbar-http.png"), 2)

  $bubble = Open-Bubble $shell
  $captions = [XenonSiteFixture]::Captions($bubble)
  Require ($captions -contains '127.0.0.1' -and $captions -contains 'Connection is not secure' -and $captions -contains 'Cookies and site data' -and $captions -contains 'Site settings') "Bubble rows: $($captions -join ' | ')"
  Require ([XenonSiteFixture]::GetDlgItem($bubble, $SiteSetting + $PopupsIndex) -eq [IntPtr]::Zero) 'Unchanged settings are not listed in the bubble'
  [XenonSiteFixture]::Capture($bubble, (Join-Path $taskOut "$taskRun-bubble.png"))
  [XenonSiteFixture]::Click($bubble, $SiteConnection)
  Wait-Until { [XenonSiteFixture]::Captions($bubble) -contains 'This page is served from this computer over HTTP, without encryption.' } 'connection details' | Out-Null
  Require ([XenonSiteFixture]::GetDlgItem($bubble, $SiteCertificate) -eq [IntPtr]::Zero) 'HTTP has no certificate row'
  [XenonSiteFixture]::Click($bubble, $SiteBack)
  Wait-Until { [XenonSiteFixture]::Captions($bubble) -contains 'Cookies and site data' } 'main view after Back' | Out-Null
  Record 'HTTP from this computer shows its connection state, cookies row and site settings'

  # A setting changed in Site settings appears in the bubble until reset there.
  [XenonSiteFixture]::Click($bubble, $SiteSettings)
  $settings = Wait-Panel 'Xenon Site Settings'
  Wait-Until { [XenonSiteFixture]::IsWindowEnabled([XenonSiteFixture]::GetDlgItem($settings, $PanelSetting)) } 'site settings' | Out-Null
  Require ([XenonSiteFixture]::Find($taskProcess.Id, 'XenonSiteInfo', $null) -eq [IntPtr]::Zero) 'Opening a panel closes the bubble'
  foreach ($n in 0..5) { Require ((Combo-Value $settings ($PanelSetting + $n)) -eq 0) "Setting $n starts at its default" }
  $shade = [XenonSiteFixture]::Brightness([XenonSiteFixture]::GetDlgItem($settings, $PanelSetting))
  Require ($shade -lt 110) "Dark combo boxes render dark (mean luminance $([Math]::Round($shade)))"
  [XenonSiteFixture]::Capture($settings, (Join-Path $taskOut "$taskRun-settings-dark.png"), 2)
  Record "Dark site settings use dark combo boxes (mean luminance $([Math]::Round($shade)))"
  Require (([XenonSiteFixture]::Captions($settings) -join "`n") -like '*Site: http://127.0.0.1:*2 cookies*') "Site settings summary: $([XenonSiteFixture]::Captions($settings) -join ' | ')"
  $combo = [XenonSiteFixture]::GetDlgItem($settings, $PanelSetting + $PopupsIndex)
  [void][XenonSiteFixture]::Send($combo, $CB_SETCURSEL, [IntPtr]1, [IntPtr]::Zero)
  [void][XenonSiteFixture]::PostMessage($settings, $WM_COMMAND, [IntPtr](($CBN_SELCHANGE -shl 16) -bor ($PanelSetting + $PopupsIndex)), $combo)
  Start-Sleep -Milliseconds 600
  [void][XenonSiteFixture]::PostMessage($settings, $WM_CLOSE, [IntPtr]::Zero, [IntPtr]::Zero)
  $bubble = Open-Bubble $shell
  Wait-Until { [XenonSiteFixture]::GetDlgItem($bubble, $SiteSetting + $PopupsIndex) -ne [IntPtr]::Zero } 'changed setting in the bubble' | Out-Null
  Require ((Combo-Value $bubble ($SiteSetting + $PopupsIndex)) -eq 1 -and [XenonSiteFixture]::Captions($bubble) -contains 'Pop-ups and redirects') 'The bubble shows Pop-ups as Allow'
  [XenonSiteFixture]::Click($bubble, $SiteReset)
  Wait-Until { $w = [XenonSiteFixture]::Find($taskProcess.Id, 'XenonSiteInfo', $null); if ($w -ne [IntPtr]::Zero -and [XenonSiteFixture]::GetDlgItem($w, $SiteSetting + $PopupsIndex) -eq [IntPtr]::Zero -and [XenonSiteFixture]::Captions($w) -contains 'Site settings') { $w } } 'reset bubble' | Out-Null
  Record 'Site settings persist per site, appear in the bubble and reset'

  # Cookie metadata lists and deletes; the website stops receiving deleted cookies.
  $bubble = [XenonSiteFixture]::Find($taskProcess.Id, 'XenonSiteInfo', $null)
  [XenonSiteFixture]::Click($bubble, $SiteCookies)
  $cookies = Wait-Panel 'Xenon Cookies and Site Data'
  $list = [XenonSiteFixture]::GetDlgItem($cookies, 11)
  Wait-Until { [int][XenonSiteFixture]::Send($list, $LVM_GETITEMCOUNT, [IntPtr]::Zero, [IntPtr]::Zero) -eq 2 } 'two listed cookies' | Out-Null
  Require (([XenonSiteFixture]::Captions($cookies) -join "`n") -like '*Cookie values are never shown.*') 'The cookie panel states that values are hidden'
  [XenonSiteFixture]::Capture($cookies, (Join-Path $taskOut "$taskRun-cookies.png"))
  [void][XenonSiteFixture]::PostMessage($list, $WM_KEYDOWN, [IntPtr]$VK_HOME, [IntPtr]::Zero)
  Wait-Until { [int][XenonSiteFixture]::Send($list, $LVM_GETSELECTEDCOUNT, [IntPtr]::Zero, [IntPtr]::Zero) -eq 1 } 'one selected cookie' | Out-Null
  [XenonSiteFixture]::Click($cookies, $PanelLeft)
  Wait-Until { [int][XenonSiteFixture]::Send($list, $LVM_GETITEMCOUNT, [IntPtr]::Zero, [IntPtr]::Zero) -eq 1 } 'one remaining cookie' | Out-Null
  Open-Page $shell "$base/after-one" 'Site info fixture /after-one'
  Require ((Last-Request '/after-one') -match ' COOKIES xenon_fixture_[ab]$') "One cookie remains for the website: $((Last-Request '/after-one'))"
  Record 'Deleting a selected cookie removes it from the website'

  [XenonSiteFixture]::Click($cookies, $PanelRight)
  $confirm = Wait-Until { $w = [XenonSiteFixture]::Find($taskProcess.Id, '#32770', 'Delete site data'); if ($w -ne [IntPtr]::Zero) { $w } } 'delete confirmation'
  Require (([XenonSiteFixture]::Captions($confirm) -join "`n") -like '*Delete all cookies and stored data for http://127.0.0.1:*') 'The confirmation names the site'
  [void][XenonSiteFixture]::PostMessage($confirm, $WM_COMMAND, [IntPtr]1, [IntPtr]::Zero)
  Wait-Until { [int][XenonSiteFixture]::Send($list, $LVM_GETITEMCOUNT, [IntPtr]::Zero, [IntPtr]::Zero) -eq 0 -and ([XenonSiteFixture]::Captions($cookies) -join "`n") -like '*Site data deleted*' } 'deleted site data' | Out-Null
  Open-Page $shell "$base/after-all" 'Site info fixture /after-all'
  Require ((Last-Request '/after-all') -like '* COOKIES -') 'No fixture cookies reach the website'
  [void][XenonSiteFixture]::PostMessage($cookies, $WM_CLOSE, [IntPtr]::Zero, [IntPtr]::Zero)
  Record 'Delete all site data asks first, then removes the remaining cookies'
  Stop-Fixture $shell

  if (Test-Path -LiteralPath $taskFixtureBinary) {
    # The unshipped fixture build accepts only its pinned loopback test certificate.
    $taskAuthServer = Start-Process -FilePath 'node' -ArgumentList ('"' + (Join-Path $taskRoot 'tests/fixtures/auth-server.mjs') + '"') -WindowStyle Hidden -PassThru
    $shell = Start-Fixture $taskFixtureBinary 'https'
    Start-Sleep -Seconds 1
    Open-Page $shell 'https://127.0.0.1:18766/login?case=site-info' 'Authentication fixture'
    Start-Sleep -Milliseconds 700; [XenonSiteFixture]::Capture($shell, (Join-Path $taskOut "$taskRun-toolbar-https.png"), 2)
    $bubble = Open-Bubble $shell
    Require ([XenonSiteFixture]::Captions($bubble) -contains 'Connection is not secure') 'A certificate exception is reported as not secure'
    [XenonSiteFixture]::Click($bubble, $SiteConnection)
    Wait-Until { (([XenonSiteFixture]::Captions($bubble) -join "`n") -like "*This site's certificate has a problem*") -and [XenonSiteFixture]::GetDlgItem($bubble, $SiteCertificate) -ne [IntPtr]::Zero } 'certificate details' | Out-Null
    $details = [XenonSiteFixture]::Captions($bubble) -join "`n"
    Require ($details -like '*Issued to: *' -and $details -like '*Issued by: *' -and $details -like '*Valid until: *' -and $details -like '*Protocol: TLS*') "Certificate summary: $details"
    [XenonSiteFixture]::Capture($bubble, (Join-Path $taskOut "$taskRun-certificate.png"))
    Record 'A certificate error shows its warning and certificate summary'
    [XenonSiteFixture]::Click($bubble, $SiteCertificate)
    $viewer = Wait-Until { $w = [XenonSiteFixture]::Find($taskProcess.Id, '#32770', 'Certificate'); if ($w -ne [IntPtr]::Zero) { $w } } 'Windows certificate viewer'
    Require ([XenonSiteFixture]::Find($taskProcess.Id, 'XenonSiteInfo', $null) -eq [IntPtr]::Zero) 'The bubble closes before the viewer opens'
    [void][XenonSiteFixture]::PostMessage($viewer, $WM_COMMAND, [IntPtr]1, [IntPtr]::Zero)
    Wait-Until { [XenonSiteFixture]::Find($taskProcess.Id, '#32770', 'Certificate') -eq [IntPtr]::Zero } 'viewer closing' | Out-Null
    Record 'Certificate details open the Windows certificate viewer'
    Stop-Fixture $shell
  } else { $expected = 5; Write-Host 'SKIP XenonAuthTest is not built; certificate checks did not run' }
} catch {
  $taskResults.Add(@{name='Site info integration';passed=$false;error=$_.Exception.Message})
  Write-Host "FAIL $($_.Exception.Message)"
} finally {
  if ($taskProcess -and -not $taskProcess.HasExited) {
    $shell = [XenonSiteFixture]::Find($taskProcess.Id, 'XenonBrowserShell', $null)
    if ($shell -ne [IntPtr]::Zero) { [void][XenonSiteFixture]::PostMessage($shell, $WM_CLOSE, [IntPtr]::Zero, [IntPtr]::Zero) }
    if (-not $taskProcess.WaitForExit(30000)) { $taskResults.Add(@{name='Fixture cleanup';passed=$false;error='Fixture could not close normally'}) }
  }
  foreach ($server in @($taskServer, $taskAuthServer)) { if ($server -and -not $server.HasExited) { Stop-Process -Id $server.Id -Force } }
  $taskHashAtEnd = (Get-FileHash -LiteralPath $taskDll -Algorithm SHA256).Hash
  if ($taskHashAtEnd -ne $taskHash) { $taskResults.Add(@{name='Binary stability';passed=$false;error='Application DLL changed during the test'}) }
  $requests = @(if (Test-Path $taskServerLog) { Get-Content -LiteralPath $taskServerLog | Where-Object { $_ -like 'GET *' } | ForEach-Object { "$_" } })
  $passed = $taskResults.Count -eq $expected -and @($taskResults | Where-Object { -not $_.passed }).Count -eq 0
  $report = @{run=$taskRun;capturedAt=[DateTime]::UtcNow.ToString('o');binary='build/app/Release/Xenon.exe';applicationDllSha256=$taskHash;applicationDllSha256AtEnd=$taskHashAtEnd;certificateFixture=(Test-Path -LiteralPath $taskFixtureBinary);profiles=$taskProfiles;coverage='native message driver with Xenon thread activation in an off-screen window; no physical input, foreground focus or on-screen visual acceptance';passed=$passed;loopbackRequests=$requests;results=$taskResults}
  $report | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $taskOut "$taskRun.json") -Encoding utf8
  Write-Host "Report: out/$taskRun.json"
}
if (-not $passed) { exit 1 }
