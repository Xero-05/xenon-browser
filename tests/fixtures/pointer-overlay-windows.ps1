param([Parameter(Mandatory=$true)][int]$ProcessId)
# Reads the screen rectangles of one disposable test browser's shell, visible
# page window, agent pointer overlay and element highlight overlay, found by its
# process ID. Read-only: no messages are sent and no input is injected.
$ErrorActionPreference = 'Stop'
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
using System.Text;
public static class XenonOverlayWindows {
  public delegate bool EnumProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc f, IntPtr l);
  [DllImport("user32.dll")] public static extern bool EnumChildWindows(IntPtr p, EnumProc f, IntPtr l);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassNameW(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out Rect r);
  [DllImport("user32.dll")] public static extern uint GetDpiForWindow(IntPtr h);
  public struct Rect { public int left, top, right, bottom; }
  static string Cls(IntPtr h) { var s = new StringBuilder(256); GetClassNameW(h, s, 256); return s.ToString(); }
  public static IntPtr Top(uint pid, string cls) {
    IntPtr found = IntPtr.Zero;
    EnumWindows((h, l) => { uint p; GetWindowThreadProcessId(h, out p); if (p == pid && Cls(h) == cls) { found = h; return false; } return true; }, IntPtr.Zero);
    return found;
  }
  public static IntPtr VisibleChild(IntPtr root, string cls) {
    IntPtr found = IntPtr.Zero;
    EnumChildWindows(root, (h, l) => { if (IsWindowVisible(h) && Cls(h) == cls) { found = h; return false; } return true; }, IntPtr.Zero);
    return found;
  }
}
'@
function Describe([IntPtr]$window) {
  if ($window -eq [IntPtr]::Zero) { return $null }
  $rect = New-Object XenonOverlayWindows+Rect
  [void][XenonOverlayWindows]::GetWindowRect($window, [ref]$rect)
  @{ left = $rect.left; top = $rect.top; right = $rect.right; bottom = $rect.bottom; visible = [XenonOverlayWindows]::IsWindowVisible($window) }
}
$shell = [XenonOverlayWindows]::Top([uint32]$ProcessId, 'XenonBrowserShell')
if ($shell -eq [IntPtr]::Zero) { throw 'No Xenon shell window for this process' }
$page = [XenonOverlayWindows]::VisibleChild($shell, 'CefBrowserWindow')
@{
  dpi = [XenonOverlayWindows]::GetDpiForWindow($shell)
  shell = Describe $shell
  page = Describe $page
  cursor = Describe ([XenonOverlayWindows]::Top([uint32]$ProcessId, 'XenonAgentCursor'))
  highlight = Describe ([XenonOverlayWindows]::VisibleChild($shell, 'XenonElementOverlay'))
} | ConvertTo-Json -Compress
