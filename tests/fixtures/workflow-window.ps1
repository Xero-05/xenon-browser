param([int]$BrowserPid, [string]$Profile,
  [ValidateSet('snapshot','minimize','restore','foreground')][string]$Action = 'snapshot',
  [int]$Width = 0, [int]$Height = 0)
$ErrorActionPreference = 'Stop'
$taskRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../.cache'))
$taskProfile = [IO.Path]::GetFullPath($Profile)
if ([IO.Path]::GetDirectoryName($taskProfile) -cne $taskRoot -or
    -not [IO.Path]::GetFileName($taskProfile).StartsWith('workflow-viewport-') -or
    (Get-Content -LiteralPath (Join-Path $taskProfile 'SYNTHETIC_TEST_PROFILE') -Raw).Trim() -cne 'XENON_SYNTHETIC_WORKFLOW_VIEWPORT_FIXTURE') {
  throw 'Only a disposable workflow-viewport fixture is accepted.'
}
$taskProcess = Get-CimInstance Win32_Process -Filter "ProcessId=$BrowserPid"
if (-not $taskProcess -or -not $taskProcess.CommandLine.Contains("--user-data-dir=$taskProfile")) {
  throw 'Target process does not belong to this fixture.'
}
Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;
public static class WorkflowWindow {
  delegate bool EnumProc(IntPtr h, IntPtr l);
  [StructLayout(LayoutKind.Sequential)] public struct Rect { public int l,t,r,b; }
  [DllImport("user32.dll")] static extern bool EnumWindows(EnumProc p,IntPtr l);
  [DllImport("user32.dll")] static extern bool EnumChildWindows(IntPtr h,EnumProc p,IntPtr l);
  [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr h,out uint p);
  [DllImport("user32.dll",CharSet=CharSet.Unicode)] static extern int GetClassName(IntPtr h,StringBuilder s,int n);
  [DllImport("user32.dll")] static extern bool GetClientRect(IntPtr h,out Rect r);
  [DllImport("user32.dll")] public static extern bool IsIconic(IntPtr h);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h,int cmd);
  [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h,IntPtr a,int x,int y,int w,int z,uint f);
  [DllImport("user32.dll")] public static extern bool SetProcessDpiAwarenessContext(IntPtr c);
  static string Cls(IntPtr h) {var s=new StringBuilder(128);GetClassName(h,s,128);return s.ToString();}
  public static IntPtr Find(uint pid) {IntPtr result=IntPtr.Zero;EnumWindows((h,l)=>{uint p;GetWindowThreadProcessId(h,out p);if(p==pid&&Cls(h)=="XenonBrowserShell"){result=h;return false;}return true;},IntPtr.Zero);return result;}
  static string Info(IntPtr h) {Rect r;GetClientRect(h,out r);return Cls(h)+":"+(r.r-r.l)+"x"+(r.b-r.t)+":"+IsWindowVisible(h);}
  public static string[] Snapshot(IntPtr shell) {var rows=new List<string>();rows.Add(Info(shell));EnumChildWindows(shell,(h,l)=>{var cls=Cls(h);if(cls=="XenonTabHost"||cls.StartsWith("Chrome_"))rows.Add(Info(h));return true;},IntPtr.Zero);return rows.ToArray();}
  public static string[] Top(uint pid) {var rows=new List<string>();EnumWindows((h,l)=>{uint p;GetWindowThreadProcessId(h,out p);if(p==pid)rows.Add(h.ToInt64()+":"+Info(h));return true;},IntPtr.Zero);return rows.ToArray();}
}
'@
[void][WorkflowWindow]::SetProcessDpiAwarenessContext([IntPtr](-4))
$taskWindow = [WorkflowWindow]::Find([uint32]$BrowserPid)
if ($taskWindow -eq [IntPtr]::Zero) { throw 'Synthetic shell not found.' }
if ($Action -eq 'minimize') { [void][WorkflowWindow]::ShowWindow($taskWindow,6) }
if ($Action -eq 'restore') { [void][WorkflowWindow]::ShowWindow($taskWindow,9) }
if ($Width -gt 0 -and $Height -gt 0) {
  if (-not [WorkflowWindow]::SetWindowPos($taskWindow,[IntPtr]::Zero,0,0,$Width,$Height,22)) { throw 'Synthetic resize failed.' }
}
if ($Action -eq 'foreground') {
  & (Join-Path $PSScriptRoot 'popup-windows.ps1') -ProcessId $BrowserPid -Action foreground | Out-Null
}
@{ minimized=[WorkflowWindow]::IsIconic($taskWindow); windows=@([WorkflowWindow]::Snapshot($taskWindow)); top=@([WorkflowWindow]::Top([uint32]$BrowserPid)) } | ConvertTo-Json -Compress
