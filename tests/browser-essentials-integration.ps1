# Native message-driver regression for links Windows passes to Xenon, site
# icons, the bookmark manager, history, links for other apps, screen-sharing
# prompts, Clear browsing data and Settings. It operates only the process it
# launches, with a generated empty profile and loopback fixtures, and parks
# every window it opens beyond every monitor. Results are checked in the test
# profile's own encrypted metadata. A synthetic link scheme is registered in
# HKCU for the run and removed afterwards; its app only records its arguments.
# This is not physical input or on-screen visual acceptance, and it never
# enters full screen or allows screen sharing, which would cover or capture
# the person's real screen.
param()
$ErrorActionPreference = 'Stop'
$taskRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$taskRun = 'essentials-' + [Guid]::NewGuid().ToString('N')
$taskBinary = Join-Path $taskRoot 'build/app/Release/Xenon.exe'
$taskDll = Join-Path $taskRoot 'build/app/Release/Xenon.dll'
$taskResults = [Collections.Generic.List[object]]::new()
$taskHash = (Get-FileHash -LiteralPath $taskDll -Algorithm SHA256).Hash
$taskOut = Join-Path $taskRoot 'out'
New-Item -ItemType Directory -Force -Path $taskOut | Out-Null
Add-Type -AssemblyName System.Drawing, System.Windows.Forms, System.Security
Add-Type -ReferencedAssemblies System.Drawing -TypeDefinition @'
using System; using System.Collections.Generic; using System.Drawing; using System.Drawing.Imaging; using System.Runtime.InteropServices; using System.Text;
public static class XenonEssentials {
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
  [DllImport("user32.dll")] public static extern bool IsWindow(IntPtr window);
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr window, int command);
  [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr window, IntPtr after, int x, int y, int cx, int cy, uint flags);
  [DllImport("user32.dll")] public static extern bool SetProcessDpiAwarenessContext(IntPtr value);
  [DllImport("user32.dll")] static extern bool PrintWindow(IntPtr window, IntPtr dc, uint flags);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr window, out RECT rect);
  [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr window, out RECT rect);
  static string Class(IntPtr window) { var s = new StringBuilder(128); GetClassName(window, s, s.Capacity); return s.ToString(); }
  public static string Text(IntPtr window) { var s = new StringBuilder(4096); GetWindowText(window, s, s.Capacity); return s.ToString(); }
  public static IntPtr Find(int pid, string kind, string caption) {
    IntPtr result = IntPtr.Zero;
    EnumWindows((window, data) => { uint owner; GetWindowThreadProcessId(window, out owner);
      if (owner == pid && Class(window) == kind && IsWindowVisible(window) && (String.IsNullOrEmpty(caption) || Text(window) == caption)) { result = window; return false; } return true; }, IntPtr.Zero);
    return result;
  }
  // The topmost visible page widget: the selected tab's host is above the others.
  public static IntPtr PageWidget(IntPtr shell) {
    IntPtr result = IntPtr.Zero;
    EnumChildWindows(shell, (child, data) => { if (Class(child) == "Chrome_RenderWidgetHostHWND" && IsWindowVisible(child)) { result = child; return false; } return true; }, IntPtr.Zero);
    return result;
  }
  public static string[] Captions(IntPtr window) { var values = new List<string>(); EnumChildWindows(window, (child, data) => { if (IsWindowVisible(child)) values.Add(Text(child)); return true; }, IntPtr.Zero); return values.ToArray(); }
  public static void Click(IntPtr parent, int id) { var control = GetDlgItem(parent, id); if (control == IntPtr.Zero || !PostMessage(parent, 0x111, new IntPtr(id), control)) throw new Exception("Fixture click failed: " + id); }
  // Strongly red pixels in the left `width` pixels of a rendered window: the
  // fixture's red site icon in the tab list.
  public static int RedPixels(IntPtr window, int width) {
    RECT rect; GetWindowRect(window, out rect); int count = 0;
    using (var bitmap = new Bitmap(Math.Max(1, rect.Right - rect.Left), Math.Max(1, rect.Bottom - rect.Top))) {
      using (var graphics = Graphics.FromImage(bitmap)) { var dc = graphics.GetHdc(); PrintWindow(window, dc, 2); graphics.ReleaseHdc(dc); }
      for (int y = 0; y < bitmap.Height; ++y) for (int x = 0; x < Math.Min(width, bitmap.Width); ++x) { var c = bitmap.GetPixel(x, y); if (c.R > 200 && c.G < 70 && c.B < 70) ++count; }
    }
    return count;
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
try { [void][XenonEssentials]::SetProcessDpiAwarenessContext([IntPtr](-4)) } catch {}
$WM_SETTEXT = 0x000C; $WM_KEYDOWN = 0x0100; $WM_CLOSE = 0x0010; $WM_COMMAND = 0x0111; $WM_MOUSEMOVE = 0x0200; $WM_LBUTTONDOWN = 0x0201; $WM_LBUTTONUP = 0x0202
$VK_RETURN = 0x0D; $VK_HOME = 0x24; $CB_GETCURSEL = 0x147; $CB_SETCURSEL = 0x14E; $CBN_SELCHANGE = 1; $LVM_GETITEMCOUNT = 0x1004; $LVM_GETEDITCONTROL = 0x1018
# Shell menu commands and library/form control IDs from native/src/cef_shell.cpp.
$MenuBookmark = 2104; $MenuBookmarks = 2105; $MenuHistory = 2106; $MenuSettings = 2134; $MenuClearData = 2135
$LibraryOpen = 1; $LibraryNewFolder = 3; $LibraryDelete = 6; $LibraryList = 11; $LibrarySearch = 13
$FormPrimary = 1; $FormSearchEngine = 20; $FormDefaultStatus = 21; $FormMakeDefault = 22
$taskParked = [System.Windows.Forms.SystemInformation]::VirtualScreen.Left - 2400
function Wait-Until([scriptblock]$Action, [string]$Description, [int]$Seconds = 30) {
  $deadline = [DateTime]::UtcNow.AddSeconds($Seconds)
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
function Park($Window) { [void][XenonEssentials]::SetWindowPos($Window, [IntPtr]1, $taskParked, 120, 0, 0, 0x0011) }
function Wait-Window([string]$Kind, [string]$Caption) { $w = Wait-Until { $f = [XenonEssentials]::Find($taskProcess.Id, $Kind, $Caption); if ($f -ne [IntPtr]::Zero) { $f } } $Caption; Park $w; return $w }
function Command($Shell, [int]$Id) { [void][XenonEssentials]::PostMessage($Shell, $WM_COMMAND, [IntPtr]$Id, [IntPtr]::Zero) }
function Count($List) { [int][XenonEssentials]::Send($List, $LVM_GETITEMCOUNT, [IntPtr]::Zero, [IntPtr]::Zero) }
function Title-Is($Shell, [string]$Title) { Wait-Until { [XenonEssentials]::Text($Shell) -eq "$Title — Xenon" } "title $Title" | Out-Null }
function Open-Page($Shell, [string]$Url, [string]$Title) {
  $address = [XenonEssentials]::GetDlgItem($Shell, 2004)
  [void][XenonEssentials]::WriteText($address, $WM_SETTEXT, [IntPtr]::Zero, $Url); [void][XenonEssentials]::PostMessage($address, $WM_KEYDOWN, [IntPtr]$VK_RETURN, [IntPtr]::Zero)
  Title-Is $Shell $Title
}
# The Personal workspace's DPAPI-protected metadata in this run's profile.
function Read-Metadata {
  $digest = -join ([Security.Cryptography.SHA256]::Create().ComputeHash([Text.Encoding]::UTF8.GetBytes('native-default')) | ForEach-Object { $_.ToString('x2') })
  $file = Join-Path $taskProfile "workspace-$digest/Xenon metadata.dpapi"
  if (-not (Test-Path -LiteralPath $file)) { return $null }
  try { $clear = [Security.Cryptography.ProtectedData]::Unprotect([IO.File]::ReadAllBytes($file), $null, 'CurrentUser') } catch { return $null }
  return ([Text.Encoding]::UTF8.GetString($clear) | ConvertFrom-Json)
}
function Last-Request([string]$Path) { Get-Content -LiteralPath $taskServerLog | Where-Object { $_ -like "GET $Path COOKIES *" } | Select-Object -Last 1 }

# A synthetic scheme whose registered app records the arguments it received.
# The fixture server takes it as an argument, never from a request.
$taskScheme = 'xenon-essentials-' + $taskRun.Substring(11, 12)
$taskServerLog = Join-Path $taskOut "$taskRun-server.log"
$taskServer = Start-Process -FilePath 'node' -ArgumentList ('"' + (Join-Path $taskRoot 'tests/fixtures/browser-essentials-server.mjs') + '" ' + $taskScheme) -RedirectStandardOutput $taskServerLog -WindowStyle Hidden -PassThru
$taskProcess = $null
$taskProfile = Join-Path $taskRoot ".cache/$taskRun"
$taskPipe = "xenon-$taskRun"
$taskSchemeKey = "HKCU:\Software\Classes\$taskScheme"
$taskSink = Join-Path $taskOut "$taskRun-protocol.json"
$expected = 9
try {
  $node = (Get-Command node).Source
  New-Item -Path "$taskSchemeKey\shell\open\command" -Force | Out-Null
  Set-ItemProperty -LiteralPath $taskSchemeKey -Name '(default)' -Value 'URL:Xenon essentials test link'
  Set-ItemProperty -LiteralPath $taskSchemeKey -Name 'URL Protocol' -Value ''
  Set-ItemProperty -LiteralPath "$taskSchemeKey\shell\open\command" -Name '(default)' -Value ('"' + $node + '" "' + (Join-Path $taskRoot 'tests/fixtures/protocol-sink.mjs') + '" "' + $taskSink + '" "%1"')
  $port = Wait-Until { if (Test-Path $taskServerLog) { $line = Get-Content -LiteralPath $taskServerLog | Where-Object { $_ -like 'PORT *' } | Select-Object -First 1; if ($line) { $line.Split(' ')[1] } } } 'loopback fixture server'
  $base = "http://127.0.0.1:$port"
  New-Item -ItemType Directory -Path $taskProfile | Out-Null
  Set-Content -LiteralPath (Join-Path $taskProfile 'SYNTHETIC_TEST_PROFILE') -Value 'XENON_SYNTHETIC_ESSENTIALS_FIXTURE'
  [IO.File]::WriteAllText((Join-Path $taskProfile 'ui-settings.json'), '{"version":1,"theme":"light","introductionCompleted":true,"automaticUpdateChecks":false}')

  # A link Windows passes at startup opens in the first tab; a second launch
  # while Xenon runs hands its link to the running browser.
  $script:taskProcess = Start-Process -FilePath $taskBinary -ArgumentList ('--user-data-dir="' + $taskProfile + '" --broker-pipe=' + $taskPipe + ' --single-argument ' + "$base/one") -WindowStyle Minimized -PassThru
  Write-Host "Fixture PID $($taskProcess.Id): $taskRun"
  $shell = Wait-Until { $w = [XenonEssentials]::Find($taskProcess.Id, 'XenonBrowserShell', $null); if ($w -ne [IntPtr]::Zero) { $w } } 'fixture shell'
  [void][XenonEssentials]::ShowWindow($shell, 4)
  [void][XenonEssentials]::SetWindowPos($shell, [IntPtr]1, $taskParked, 120, 1200, 760, 0x0010)
  Title-Is $shell 'Essentials one'
  Record 'A link passed at startup opens in the first Personal tab'
  $relaunch = Start-Process -FilePath $taskBinary -ArgumentList ('--user-data-dir="' + $taskProfile + '" --broker-pipe=' + $taskPipe + '-second --single-argument ' + "$base/two?a=1&b=two words") -WindowStyle Hidden -PassThru
  Require ($relaunch.WaitForExit(30000)) 'The second launch hands over and exits'
  Title-Is $shell 'Essentials two'
  $requests = @(Get-Content -LiteralPath $taskServerLog | Where-Object { $_ -like 'GET /two COOKIES *' })
  Require ($requests.Count -eq 1 -and $requests[0] -like '* QUERY ?a=1&b=two%20words') "The whole handed-over link reached the website once: $($requests -join ' | ')"
  Park $shell
  Record 'A link opened while Xenon runs opens in a new tab of the running browser'

  # The page's declared icon appears in its tab row.
  Open-Page $shell "$base/icon-page" 'Essentials icon'
  $red = Wait-Until { $n = [XenonEssentials]::RedPixels($shell, 300); if ($n -ge 100) { $n } } 'site icon in the tab list'
  [XenonEssentials]::Capture($shell, (Join-Path $taskOut "$taskRun-site-icon.png"), 2)
  Record "Site icons are drawn in the tab list ($red red pixels)"

  # Bookmark manager: a bookmark, a renamed new folder, search and deletion.
  Open-Page $shell "$base/one" 'Essentials one'
  Command $shell $MenuBookmark
  Wait-Until { $m = Read-Metadata; $m -and @($m.bookmarks | Where-Object { $_.url -eq "$base/one" }).Count -eq 1 } 'saved bookmark' | Out-Null
  Command $shell $MenuBookmarks
  $library = Wait-Window 'XenonLibrary' 'Xenon Bookmarks'
  $list = [XenonEssentials]::GetDlgItem($library, $LibraryList)
  Wait-Until { (Count $list) -eq 1 } 'one listed bookmark' | Out-Null
  [XenonEssentials]::Click($library, $LibraryNewFolder)
  $edit = Wait-Until { $e = [XenonEssentials]::Send($list, $LVM_GETEDITCONTROL, [IntPtr]::Zero, [IntPtr]::Zero); if ($e -ne [IntPtr]::Zero) { $e } } 'new folder name editor'
  [void][XenonEssentials]::WriteText($edit, $WM_SETTEXT, [IntPtr]::Zero, 'Probe folder')
  [void][XenonEssentials]::PostMessage($edit, $WM_KEYDOWN, [IntPtr]$VK_RETURN, [IntPtr]::Zero)
  Wait-Until { $m = Read-Metadata; $m -and @($m.folders | Where-Object { $_.name -eq 'Probe folder' }).Count -eq 1 } 'renamed folder' | Out-Null
  # Enter commits the name; it does not open the new folder.
  Wait-Until { (Count $list) -eq 2 } 'folder and bookmark listed' | Out-Null
  [void][XenonEssentials]::WriteText([XenonEssentials]::GetDlgItem($library, $LibrarySearch), $WM_SETTEXT, [IntPtr]::Zero, 'essentials ONE')
  Wait-Until { (Count $list) -eq 1 } 'search result' | Out-Null
  [void][XenonEssentials]::WriteText([XenonEssentials]::GetDlgItem($library, $LibrarySearch), $WM_SETTEXT, [IntPtr]::Zero, '')
  Wait-Until { (Count $list) -eq 2 } 'cleared search' | Out-Null
  [XenonEssentials]::Capture($library, (Join-Path $taskOut "$taskRun-bookmarks.png"), 2)
  [void][XenonEssentials]::PostMessage($list, $WM_KEYDOWN, [IntPtr]$VK_HOME, [IntPtr]::Zero); Start-Sleep -Milliseconds 300
  [XenonEssentials]::Click($library, $LibraryDelete)
  Wait-Until { $m = Read-Metadata; $m -and @($m.folders).Count -eq 0 -and @($m.bookmarks).Count -eq 1 } 'deleted empty folder' | Out-Null
  [void][XenonEssentials]::PostMessage($library, $WM_CLOSE, [IntPtr]::Zero, [IntPtr]::Zero)
  Record 'The bookmark manager creates, renames, searches and deletes folders'

  # History: search narrows the list and Delete removes exactly that page.
  Open-Page $shell "$base/three" 'Essentials three'
  Wait-Until { $m = Read-Metadata; $m -and @($m.history | Where-Object { $_.url -eq "$base/three" }).Count -ge 1 } 'recorded visit' -Seconds 15 | Out-Null
  Command $shell $MenuHistory
  $history = Wait-Window 'XenonLibrary' 'Xenon History'
  $list = [XenonEssentials]::GetDlgItem($history, $LibraryList)
  Wait-Until { (Count $list) -ge 4 } 'listed visits' | Out-Null
  [void][XenonEssentials]::WriteText([XenonEssentials]::GetDlgItem($history, $LibrarySearch), $WM_SETTEXT, [IntPtr]::Zero, 'essentials three')
  Wait-Until { (Count $list) -eq 1 } 'matching visit' | Out-Null
  [XenonEssentials]::Capture($history, (Join-Path $taskOut "$taskRun-history.png"), 2)
  [void][XenonEssentials]::PostMessage($list, $WM_KEYDOWN, [IntPtr]$VK_HOME, [IntPtr]::Zero); Start-Sleep -Milliseconds 300
  [XenonEssentials]::Click($history, $LibraryDelete)
  Wait-Until { (Count $list) -eq 0 } 'deleted visit' | Out-Null
  $m = Read-Metadata
  Require (@($m.history | Where-Object { $_.url -eq "$base/three" }).Count -eq 0 -and @($m.history | Where-Object { $_.url -eq "$base/one" }).Count -ge 1) 'Only the selected page left history'
  [void][XenonEssentials]::PostMessage($history, $WM_CLOSE, [IntPtr]::Zero, [IntPtr]::Zero)
  Record 'History search finds a page and Delete removes only it'

  # A site link for another app asks first, then reaches the registered app as
  # one escaped argument. Dangerous schemes never reach a prompt.
  Open-Page $shell "$base/app-link" 'Essentials app link'
  $prompt = Wait-Window 'XenonBrowserPanel' 'Open link in another app'
  $text = [XenonEssentials]::Captions($prompt) -join "`n"
  Require ($text -like "*Website: http://127.0.0.1:$port*" -and $text -like "*$($taskScheme): link*") "App-link prompt: $text"
  [XenonEssentials]::Capture($prompt, (Join-Path $taskOut "$taskRun-app-link.png"), 2)
  [XenonEssentials]::Click($prompt, 1)
  Wait-Until { Test-Path -LiteralPath $taskSink } 'registered app' | Out-Null; Start-Sleep -Milliseconds 200
  $received = @(Get-Content -LiteralPath $taskSink -Raw | ConvertFrom-Json)
  Require ($received.Count -eq 1 -and $received[0] -ceq "$($taskScheme):probe%20value%20%22quoted%22&x=1") "The app received: $($received -join ' | ')"
  Open-Page $shell "$base/denied-link" 'Essentials denied link'
  Start-Sleep -Milliseconds 1500
  Require ([XenonEssentials]::Find($taskProcess.Id, 'XenonBrowserPanel', 'Open link in another app') -eq [IntPtr]::Zero) 'A denied scheme shows no prompt'
  Record 'Links for other apps need confirmation, arrive escaped, and dangerous schemes are refused'

  # Screen sharing names the whole screen and a denial reaches the page.
  Open-Page $shell "$base/share" 'Essentials share'
  $widget = Wait-Until { $w = [XenonEssentials]::PageWidget($shell); if ($w -ne [IntPtr]::Zero) { $w } } 'page widget'
  $client = New-Object XenonEssentials+RECT; [void][XenonEssentials]::GetClientRect($widget, [ref]$client)
  $point = [IntPtr](([int]($client.Bottom / 2) -shl 16) -bor [int]($client.Right / 2))
  [void][XenonEssentials]::PostMessage($widget, $WM_MOUSEMOVE, [IntPtr]::Zero, $point)
  [void][XenonEssentials]::PostMessage($widget, $WM_LBUTTONDOWN, [IntPtr]1, $point); Start-Sleep -Milliseconds 80
  [void][XenonEssentials]::PostMessage($widget, $WM_LBUTTONUP, [IntPtr]::Zero, $point)
  $prompt = Wait-Window 'XenonBrowserPanel' 'Website permission request'
  $text = [XenonEssentials]::Captions($prompt) -join "`n"
  Require ($text -like '*Your entire screen*' -and $text -like '*cannot share a single window or tab*') "Screen-share prompt: $text"
  [XenonEssentials]::Capture($prompt, (Join-Path $taskOut "$taskRun-share.png"), 2)
  [XenonEssentials]::Click($prompt, 2)
  Title-Is $shell 'Share NotAllowedError'
  Record 'Screen sharing asks first, names the whole screen, and a denial reaches the page'

  # Clear browsing data removes the workspace's history and cookies.
  Open-Page $shell "$base/cookie" 'Essentials cookie'
  Open-Page $shell "$base/before-clear" 'Essentials before-clear'
  Require ((Last-Request '/before-clear') -like '* COOKIES xenon_essentials QUERY *') 'The fixture cookie was set'
  Command $shell $MenuClearData
  $clear = Wait-Window 'XenonForm' 'Clear browsing data'
  [XenonEssentials]::Capture($clear, (Join-Path $taskOut "$taskRun-clear.png"), 2)
  [XenonEssentials]::Click($clear, $FormPrimary)
  Wait-Until { -not [XenonEssentials]::IsWindow($clear) } 'clear dialog closing' | Out-Null; Start-Sleep -Milliseconds 500
  Wait-Until { $m = Read-Metadata; $m -and @($m.history).Count -eq 0 } 'cleared history' | Out-Null
  Open-Page $shell "$base/after-clear" 'Essentials after-clear'
  Wait-Until { (Last-Request '/after-clear') -like '* COOKIES - QUERY *' } 'request without cookies' -Seconds 5 | Out-Null
  Record 'Clear browsing data removes history and cookies for the workspace'

  # Settings: the search engine is saved; this development copy is not the
  # registered default browser, so Windows' chooser is not offered.
  Command $shell $MenuSettings
  $settings = Wait-Window 'XenonForm' 'Xenon Settings'
  $combo = [XenonEssentials]::GetDlgItem($settings, $FormSearchEngine)
  Require ([int][XenonEssentials]::Send($combo, $CB_GETCURSEL, [IntPtr]::Zero, [IntPtr]::Zero) -eq 0) 'Google is the default search engine'
  [void][XenonEssentials]::Send($combo, $CB_SETCURSEL, [IntPtr]1, [IntPtr]::Zero)
  [void][XenonEssentials]::PostMessage($settings, $WM_COMMAND, [IntPtr](($CBN_SELCHANGE -shl 16) -bor $FormSearchEngine), $combo)
  Wait-Until { (Get-Content -LiteralPath (Join-Path $taskProfile 'ui-settings.json') -Raw) -like '*"searchEngine":"bing"*' } 'saved search engine' | Out-Null
  Require ([XenonEssentials]::Text([XenonEssentials]::GetDlgItem($settings, $FormDefaultStatus)) -like '*not registered with Windows*' -and -not [XenonEssentials]::IsWindowEnabled([XenonEssentials]::GetDlgItem($settings, $FormMakeDefault))) 'An unregistered copy explains how to become the default browser'
  [XenonEssentials]::Capture($settings, (Join-Path $taskOut "$taskRun-settings.png"), 2)
  [void][XenonEssentials]::PostMessage($settings, $WM_CLOSE, [IntPtr]::Zero, [IntPtr]::Zero)
  Record 'Settings saves the search engine and reports default-browser registration'

  [void][XenonEssentials]::PostMessage($shell, $WM_CLOSE, [IntPtr]::Zero, [IntPtr]::Zero)
  Require ($taskProcess.WaitForExit(30000) -and $taskProcess.ExitCode -eq 0) 'Fixture closes normally'
  $script:taskProcess = $null
} catch {
  $taskResults.Add(@{name='Browser essentials integration';passed=$false;error=$_.Exception.Message})
  Write-Host "FAIL $($_.Exception.Message)"
} finally {
  if ($taskProcess -and -not $taskProcess.HasExited) {
    $shell = [XenonEssentials]::Find($taskProcess.Id, 'XenonBrowserShell', $null)
    if ($shell -ne [IntPtr]::Zero) { [void][XenonEssentials]::PostMessage($shell, $WM_CLOSE, [IntPtr]::Zero, [IntPtr]::Zero) }
    if (-not $taskProcess.WaitForExit(30000)) { $taskResults.Add(@{name='Fixture cleanup';passed=$false;error='Fixture could not close normally'}) }
  }
  if ($taskServer -and -not $taskServer.HasExited) { Stop-Process -Id $taskServer.Id -Force }
  if (Test-Path -LiteralPath $taskSchemeKey) { Remove-Item -LiteralPath $taskSchemeKey -Recurse -Force }
  if (Test-Path -LiteralPath $taskProfile) { Remove-Item -LiteralPath $taskProfile -Recurse -Force -ErrorAction SilentlyContinue }
  $taskHashAtEnd = (Get-FileHash -LiteralPath $taskDll -Algorithm SHA256).Hash
  if ($taskHashAtEnd -ne $taskHash) { $taskResults.Add(@{name='Binary stability';passed=$false;error='Application DLL changed during the test'}) }
  $requests = @(if (Test-Path $taskServerLog) { Get-Content -LiteralPath $taskServerLog | Where-Object { $_ -like 'GET *' } | ForEach-Object { "$_" } })
  $passed = $taskResults.Count -eq $expected -and @($taskResults | Where-Object { -not $_.passed }).Count -eq 0
  $report = @{run=$taskRun;capturedAt=[DateTime]::UtcNow.ToString('o');binary='build/app/Release/Xenon.exe';applicationDllSha256=$taskHash;applicationDllSha256AtEnd=$taskHashAtEnd;coverage='native message driver with Xenon thread activation in off-screen windows; posted (not physical) page input; no foreground focus, on-screen visual acceptance, full screen or allowed screen sharing';passed=$passed;loopbackRequests=$requests;results=$taskResults}
  $report | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $taskOut "$taskRun.json") -Encoding utf8
  Write-Host "Report: out/$taskRun.json"
}
if (-not $passed) { exit 1 }
