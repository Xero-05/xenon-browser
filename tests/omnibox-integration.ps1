# Native message-driver regression for address suggestions. It operates only
# the processes it launches, with generated empty profiles and loopback pages.
# The window is shown without activation beyond every monitor, because the
# suggestion list only appears for a visible window. Focus is Xenon's own thread
# focus, not the desktop foreground. This is not physical-input coverage.
param()
$ErrorActionPreference = 'Stop'
$taskRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$taskRun = 'omnibox-' + [Guid]::NewGuid().ToString('N')
$taskBinary = Join-Path $taskRoot 'build/app/Release/Xenon.exe'
$taskDll = Join-Path $taskRoot 'build/app/Release/Xenon.dll'
$taskResults = [Collections.Generic.List[object]]::new()
$taskHash = (Get-FileHash -LiteralPath $taskDll -Algorithm SHA256).Hash
$taskOut = Join-Path $taskRoot 'out'
New-Item -ItemType Directory -Force -Path $taskOut | Out-Null
Add-Type -AssemblyName System.Drawing, System.Windows.Forms
Add-Type -ReferencedAssemblies System.Drawing -TypeDefinition @'
using System; using System.Drawing; using System.Drawing.Imaging; using System.Runtime.InteropServices; using System.Text;
public static class XenonOmniboxFixture {
  delegate bool EnumProc(IntPtr window, IntPtr data);
  [DllImport("user32.dll")] static extern bool EnumWindows(EnumProc proc, IntPtr data);
  [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr window, out uint pid);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] static extern int GetClassName(IntPtr window, StringBuilder name, int size);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] static extern int GetWindowText(IntPtr window, StringBuilder text, int size);
  [DllImport("user32.dll", CharSet=CharSet.Unicode, EntryPoint="SendMessageW")] static extern IntPtr ReadText(IntPtr window, uint message, IntPtr wp, StringBuilder text);
  [DllImport("user32.dll", CharSet=CharSet.Unicode, EntryPoint="SendMessageW")] public static extern IntPtr WriteText(IntPtr window, uint message, IntPtr wp, string text);
  [DllImport("user32.dll", EntryPoint="SendMessageW")] static extern IntPtr ReadSelection(IntPtr window, uint message, out int start, out int end);
  [DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr window, int id);
  [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr window, uint message, IntPtr wp, IntPtr lp);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr window);
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr window, int command);
  [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr window, IntPtr after, int x, int y, int cx, int cy, uint flags);
  [DllImport("user32.dll")] public static extern bool SetProcessDpiAwarenessContext(IntPtr value);
  [DllImport("user32.dll")] static extern bool PrintWindow(IntPtr window, IntPtr dc, uint flags);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr window, out RECT rect);
  [StructLayout(LayoutKind.Sequential)] struct GUITHREADINFO { public int cbSize, flags; public IntPtr active, focus, capture, menuOwner, moveSize, caret; public RECT caretRect; }
  [DllImport("user32.dll")] static extern bool GetGUIThreadInfo(uint thread, ref GUITHREADINFO info);
  static string Class(IntPtr window) { var s = new StringBuilder(128); GetClassName(window, s, s.Capacity); return s.ToString(); }
  public static IntPtr Find(int pid, string kind) {
    IntPtr result = IntPtr.Zero;
    EnumWindows((window, data) => { uint owner; GetWindowThreadProcessId(window, out owner); if (owner == pid && Class(window) == kind) { result = window; return false; } return true; }, IntPtr.Zero);
    return result;
  }
  public static string Text(IntPtr window) { var s = new StringBuilder(4096); GetWindowText(window, s, s.Capacity); return s.ToString(); }
  public static string EditText(IntPtr edit) { var s = new StringBuilder(4096); ReadText(edit, 0xD, new IntPtr(s.Capacity), s); return s.ToString(); }
  public static int[] Selection(IntPtr edit) { int start, end; ReadSelection(edit, 0xB0, out start, out end); return new[] { start, end }; }
  // The thread's keyboard focus inside Xenon, without changing the foreground window.
  public static IntPtr Focus(IntPtr window) { uint pid; var info = new GUITHREADINFO(); info.cbSize = Marshal.SizeOf(info); GetGUIThreadInfo(GetWindowThreadProcessId(window, out pid), ref info); return info.focus; }
  [DllImport("user32.dll")] public static extern bool IsIconic(IntPtr window);
  public static string Describe(IntPtr shell) { var focus = Focus(shell); return "focus " + (focus == IntPtr.Zero ? "none" : Class(focus)) + ", visible " + IsWindowVisible(shell) + ", minimized " + IsIconic(shell); }
  // flags 0 asks the window to print its own client area (WM_PRINTCLIENT).
  public static void Capture(IntPtr window, string path, uint flags) {
    RECT rect; GetWindowRect(window, out rect);
    using (var bitmap = new Bitmap(Math.Max(1, rect.Right - rect.Left), Math.Max(1, rect.Bottom - rect.Top))) {
      using (var graphics = Graphics.FromImage(bitmap)) { var dc = graphics.GetHdc(); PrintWindow(window, dc, flags); graphics.ReleaseHdc(dc); }
      bitmap.Save(path, ImageFormat.Png);
    }
  }
}
'@
# Physical window coordinates match the posted client coordinates Xenon receives.
try { [void][XenonOmniboxFixture]::SetProcessDpiAwarenessContext([IntPtr](-4)) } catch {}
$WM_SETTEXT = 0x000C; $WM_KEYDOWN = 0x0100; $WM_CHAR = 0x0102; $WM_LBUTTONDOWN = 0x0201; $WM_LBUTTONUP = 0x0202; $WM_CLOSE = 0x0010
$VK_RETURN = 0x0D; $VK_ESCAPE = 0x1B; $VK_DOWN = 0x28; $VK_BACK = 0x08
function Post($Window, $Message, $WParam, $LParam = 0) { if (-not [XenonOmniboxFixture]::PostMessage($Window, $Message, [IntPtr]$WParam, [IntPtr]$LParam)) { throw 'Fixture message failed' } }
# Characters go straight to the edit; Xenon's dialog translation would turn a
# posted key-down into a second character.
function Type-Text($Edit, [string]$Text) { foreach ($character in $Text.ToCharArray()) { Post $Edit $WM_CHAR ([int]$character) } }
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

$taskServerLog = Join-Path $taskOut "$taskRun-server.log"
$taskServer = Start-Process -FilePath 'node' -ArgumentList ('"' + (Join-Path $taskRoot 'tests/fixtures/omnibox-server.mjs') + '"') -RedirectStandardOutput $taskServerLog -WindowStyle Hidden -PassThru
$taskProcess = $null
$taskProfiles = @()
try {
  $port = Wait-Until { if (Test-Path $taskServerLog) { $line = Get-Content -LiteralPath $taskServerLog | Where-Object { $_ -like 'PORT *' } | Select-Object -First 1; if ($line) { $line.Split(' ')[1] } } } 'loopback fixture server'
  $base = "http://127.0.0.1:$port"
  foreach ($theme in 'light', 'dark') {
    $profile = Join-Path $taskRoot ".cache/$taskRun-$theme"; $taskProfiles += $profile
    New-Item -ItemType Directory -Path $profile | Out-Null
    Set-Content -LiteralPath (Join-Path $profile 'SYNTHETIC_TEST_PROFILE') -Value 'XENON_SYNTHETIC_OMNIBOX_FIXTURE'
    [IO.File]::WriteAllText((Join-Path $profile 'ui-settings.json'), "{`"version`":1,`"theme`":`"$theme`",`"introductionCompleted`":true,`"automaticUpdateChecks`":false}")
    $script:taskProcess = Start-Process -FilePath $taskBinary -ArgumentList @('--user-data-dir="' + $profile + '"', "--broker-pipe=xenon-$taskRun-$theme") -WindowStyle Minimized -PassThru
    Write-Host "Fixture PID $($taskProcess.Id): $taskRun ($theme)"
    $shell = Wait-Until { $w = [XenonOmniboxFixture]::Find($taskProcess.Id, 'XenonBrowserShell'); if ($w -ne [IntPtr]::Zero) { $w } } 'fixture shell'
    Wait-Until { [XenonOmniboxFixture]::Text($shell) -match ' — Xenon$' } 'initial blank tab' | Out-Null
    $left = [System.Windows.Forms.SystemInformation]::VirtualScreen.Left - 2400
    [void][XenonOmniboxFixture]::ShowWindow($shell, 4)
    [void][XenonOmniboxFixture]::SetWindowPos($shell, [IntPtr]1, $left, 120, 1200, 760, 0x0010)
    $address = [XenonOmniboxFixture]::GetDlgItem($shell, 2004)
    $popup = [XenonOmniboxFixture]::Find($taskProcess.Id, 'XenonAddressSuggestions')
    Require ($popup -ne [IntPtr]::Zero -and -not [XenonOmniboxFixture]::IsWindowVisible($popup)) 'The suggestion list exists hidden'

    # External WM_SETTEXT plus Enter opens the typed URL unchanged and records history.
    foreach ($page in @(@('/alpha','Alpha synthetic page'), @('/beta','Beta synthetic page'), @('/docs/guide','Synthetic omnibox guide'))) {
      [void][XenonOmniboxFixture]::WriteText($address, $WM_SETTEXT, [IntPtr]::Zero, $base + $page[0]); Post $address $WM_KEYDOWN $VK_RETURN
      Wait-Until { [XenonOmniboxFixture]::Text($shell) -eq "$($page[1]) — Xenon" } "page $($page[0])" | Out-Null
      Start-Sleep -Milliseconds 1200
    }
    Require (-not [XenonOmniboxFixture]::IsWindowVisible($popup)) 'Unfocused address text shows no list'
    Record "$theme`: externally set addresses still open exactly as typed"

    # The first click focuses the address; a late page focus command may take it back once.
    Start-Sleep -Seconds 2
    for ($attempt = 0; $attempt -lt 40 -and [XenonOmniboxFixture]::Focus($shell) -ne $address; $attempt++) {
      Post $address $WM_LBUTTONDOWN 1 0x00050005; Post $address $WM_LBUTTONUP 0 0x00050005; Start-Sleep -Milliseconds 300
    }
    Require ([XenonOmniboxFixture]::Focus($shell) -eq $address) "Address focus was not acquired: $([XenonOmniboxFixture]::Describe($shell))"
    # Each typed character completes again, so wait for the last one's selection.
    Type-Text $address '127.0'
    Wait-Until { [XenonOmniboxFixture]::EditText($address) -eq "127.0.0.1:$port" -and [XenonOmniboxFixture]::Selection($address)[0] -eq 5 } 'inline host completion' | Out-Null
    $selection = [XenonOmniboxFixture]::Selection($address)
    Require ($selection[1] -eq "127.0.0.1:$port".Length) "Only the completion is selected: $($selection -join ',')"
    Wait-Until { [XenonOmniboxFixture]::IsWindowVisible($popup) } 'suggestion list' | Out-Null
    [XenonOmniboxFixture]::Capture($popup, (Join-Path $taskOut "$taskRun-$theme-list.png"), 0)
    Record "$theme`: typing a visited host prefix completes inline and opens the list"

    Post $address $WM_KEYDOWN $VK_DOWN
    Wait-Until { [XenonOmniboxFixture]::EditText($address) -eq '127.0' } 'search row text' | Out-Null
    Post $address $WM_KEYDOWN $VK_DOWN
    Wait-Until { [XenonOmniboxFixture]::EditText($address) -eq "$base/beta" } 'history row address' | Out-Null
    Post $address $WM_KEYDOWN $VK_ESCAPE
    Wait-Until { -not [XenonOmniboxFixture]::IsWindowVisible($popup) -and [XenonOmniboxFixture]::EditText($address) -eq '127.0' } 'Escape restoring typed text' | Out-Null
    Post $address $WM_KEYDOWN $VK_DOWN
    Wait-Until { [XenonOmniboxFixture]::IsWindowVisible($popup) } 'Down reopening the list' | Out-Null
    Require ([XenonOmniboxFixture]::EditText($address) -eq '127.0') 'Reopening the list does not complete inline'
    Record "$theme`: Up/Down show row text and Escape restores what was typed"

    Type-Text $address '.'
    Wait-Until { [XenonOmniboxFixture]::EditText($address) -eq "127.0.0.1:$port" -and [XenonOmniboxFixture]::Selection($address)[0] -eq 6 } 'completion after typing again' | Out-Null
    Post $address $WM_KEYDOWN $VK_BACK
    Wait-Until { [XenonOmniboxFixture]::EditText($address) -eq '127.0.' } 'Backspace removing the completion' | Out-Null
    Start-Sleep -Milliseconds 300
    $selection = [XenonOmniboxFixture]::Selection($address)
    Require ([XenonOmniboxFixture]::EditText($address) -eq '127.0.' -and $selection[0] -eq 6 -and $selection[1] -eq 6) 'Backspace never completes again'
    Record "$theme`: Backspace removes a completion without bringing it back"

    # Five rows (completion, search, three pages); the third row is the vertical middle.
    Type-Text $address '0.1'
    Wait-Until { [XenonOmniboxFixture]::EditText($address) -eq "127.0.0.1:$port" -and [XenonOmniboxFixture]::Selection($address)[0] -eq 9 } 'completion before click' | Out-Null
    $rect = New-Object XenonOmniboxFixture+RECT; [void][XenonOmniboxFixture]::GetWindowRect($popup, [ref]$rect)
    $middle = [int](($rect.Bottom - $rect.Top) / 2)
    Post $popup $WM_LBUTTONDOWN 1 ($middle * 65536 + 60); Post $popup $WM_LBUTTONUP 0 ($middle * 65536 + 60)
    Wait-Until { [XenonOmniboxFixture]::Text($shell) -eq 'Beta synthetic page — Xenon' } 'clicked history row' | Out-Null
    Require (-not [XenonOmniboxFixture]::IsWindowVisible($popup)) 'Opening a row closes the list'
    Record "$theme`: clicking a history row opens it and closes the list"

    Post $shell $WM_CLOSE 0
    Require ($taskProcess.WaitForExit(30000)) 'Fixture did not close normally'
    Require ($taskProcess.ExitCode -eq 0) 'Fixture exit code must be zero'
    $script:taskProcess = $null
  }
} catch {
  $taskResults.Add(@{name='Omnibox integration';passed=$false;error=$_.Exception.Message})
  Write-Host "FAIL $($_.Exception.Message)"
} finally {
  if ($taskProcess -and -not $taskProcess.HasExited) {
    $shell = [XenonOmniboxFixture]::Find($taskProcess.Id, 'XenonBrowserShell')
    if ($shell -ne [IntPtr]::Zero) { [void][XenonOmniboxFixture]::PostMessage($shell, $WM_CLOSE, [IntPtr]::Zero, [IntPtr]::Zero) }
    if (-not $taskProcess.WaitForExit(30000)) { $taskResults.Add(@{name='Fixture cleanup';passed=$false;error='Fixture could not close normally'}) }
  }
  if ($taskServer -and -not $taskServer.HasExited) { Stop-Process -Id $taskServer.Id -Force }
  $taskHashAtEnd = (Get-FileHash -LiteralPath $taskDll -Algorithm SHA256).Hash
  if ($taskHashAtEnd -ne $taskHash) { $taskResults.Add(@{name='Binary stability';passed=$false;error='Application DLL changed during the test'}) }
  $requests = @(if (Test-Path $taskServerLog) { Get-Content -LiteralPath $taskServerLog | Where-Object { $_ -like 'GET *' } | ForEach-Object { "$_" } })
  $passed = $taskResults.Count -eq 10 -and @($taskResults | Where-Object { -not $_.passed }).Count -eq 0
  $report = @{run=$taskRun;capturedAt=[DateTime]::UtcNow.ToString('o');binary='build/app/Release/Xenon.exe';applicationDllSha256=$taskHash;applicationDllSha256AtEnd=$taskHashAtEnd;profiles=$taskProfiles;coverage='native message driver with Xenon thread focus in an off-screen window; no physical input, foreground focus or on-screen visual acceptance';passed=$passed;loopbackRequests=$requests;results=$taskResults}
  $report | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $taskOut "$taskRun.json") -Encoding utf8
  Write-Host "Report: out/$taskRun.json"
}
if (-not $passed) { exit 1 }
