param([Parameter(Mandatory=$true)][int]$ProcessId, [Parameter(Mandatory=$true)][int]$Command)
# Posts one menu command (WM_COMMAND) to the shell window of one disposable
# test browser, found by its process ID. Commands such as zoom act on that
# window's selected tab. No pointers cross the process boundary.
$ErrorActionPreference = 'Stop'
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
using System.Text;
public static class XenonShellCommand {
  public delegate bool EnumProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc f, IntPtr l);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassNameW(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern bool PostMessageW(IntPtr h, uint m, IntPtr w, IntPtr l);
  static string Cls(IntPtr h) { var s = new StringBuilder(256); GetClassNameW(h, s, 256); return s.ToString(); }
  public static IntPtr Shell(uint pid) {
    IntPtr frame = IntPtr.Zero;
    EnumWindows((h, l) => { uint p; GetWindowThreadProcessId(h, out p); if (p == pid && Cls(h) == "XenonBrowserShell") { frame = h; return false; } return true; }, IntPtr.Zero);
    return frame;
  }
}
'@
$shell = [XenonShellCommand]::Shell([uint32]$ProcessId)
if ($shell -eq [IntPtr]::Zero) { throw 'No Xenon shell window for this process' }
if (-not [XenonShellCommand]::PostMessageW($shell, 0x0111, [IntPtr]$Command, [IntPtr]::Zero)) { throw 'The shell command could not be posted' }  # WM_COMMAND
'posted'
