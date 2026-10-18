param([Parameter(Mandatory=$true)][int]$ProcessId, [Parameter(Mandatory=$true)][int]$Index)
# Selects a sidebar tab of one disposable test browser by posting tree-view
# messages to that process's window only. Tabs are children of the first
# workspace row, in sidebar order. No pointers cross the process boundary.
$ErrorActionPreference = 'Stop'
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
using System.Text;
public static class XenonShellTabs {
  public delegate bool EnumProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc f, IntPtr l);
  [DllImport("user32.dll")] public static extern bool EnumChildWindows(IntPtr p, EnumProc f, IntPtr l);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassNameW(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern IntPtr SendMessageW(IntPtr h, uint m, IntPtr w, IntPtr l);
  static string Cls(IntPtr h) { var s = new StringBuilder(256); GetClassNameW(h, s, 256); return s.ToString(); }
  public static IntPtr Tree(uint pid) {
    IntPtr frame = IntPtr.Zero, tree = IntPtr.Zero;
    EnumWindows((h, l) => { uint p; GetWindowThreadProcessId(h, out p); if (p == pid && Cls(h) == "XenonBrowserShell") { frame = h; return false; } return true; }, IntPtr.Zero);
    if (frame != IntPtr.Zero) EnumChildWindows(frame, (h, l) => { if (Cls(h) == "SysTreeView32") { tree = h; return false; } return true; }, IntPtr.Zero);
    return tree;
  }
}
'@
$tree = [XenonShellTabs]::Tree([uint32]$ProcessId)
if ($tree -eq [IntPtr]::Zero) { throw 'No Xenon tab tree for this process' }
$root = [XenonShellTabs]::SendMessageW($tree, 0x110A, [IntPtr]0, [IntPtr]::Zero)  # TVM_GETNEXTITEM, TVGN_ROOT
$item = [XenonShellTabs]::SendMessageW($tree, 0x110A, [IntPtr]4, $root)            # TVGN_CHILD
for ($i = 0; $i -lt $Index -and $item -ne [IntPtr]::Zero; $i++) { $item = [XenonShellTabs]::SendMessageW($tree, 0x110A, [IntPtr]1, $item) }  # TVGN_NEXT
if ($item -eq [IntPtr]::Zero) { throw "No tab at sidebar index $Index" }
[void][XenonShellTabs]::SendMessageW($tree, 0x110B, [IntPtr]9, $item)              # TVM_SELECTITEM, TVGN_CARET
'selected'
