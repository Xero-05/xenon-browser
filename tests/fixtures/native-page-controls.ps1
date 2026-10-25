param([Parameter(Mandatory=$true)][int]$BrowserPid,
  [Parameter(Mandatory=$true)][string]$Profile,
  [ValidateSet('snapshot','foreground','click','dropdown','reload','f5','accept','dismiss','place')][string]$Action='snapshot',
  [int]$X=0,[int]$Y=0,[int]$Width=1000,[int]$Height=700)
# External native messages target only this suite's disposable process. This is
# not physical input and adds no test driver or native-window MCP capability.
$ErrorActionPreference='Stop'
$taskRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../.cache'))
$taskProfile=[IO.Path]::GetFullPath($Profile)
if ([IO.Path]::GetDirectoryName($taskProfile) -cne $taskRoot -or
    -not [IO.Path]::GetFileName($taskProfile).StartsWith('native-page-controls-') -or
    (Get-Content -LiteralPath (Join-Path $taskProfile 'SYNTHETIC_TEST_PROFILE') -Raw).Trim() -cne 'XENON_SYNTHETIC_NATIVE_PAGE_CONTROLS') {
  throw 'Only a disposable native-page-controls profile is accepted.'
}
$taskProcess=Get-CimInstance Win32_Process -Filter "ProcessId=$BrowserPid"
if (-not $taskProcess -or -not $taskProcess.CommandLine.Contains("--user-data-dir=$taskProfile")) {
  throw 'Target process does not belong to this fixture.'
}
Add-Type -TypeDefinition @'
using System;
using System.Text;
using System.Collections.Generic;
using System.Runtime.InteropServices;
public static class NativePageControls {
  delegate bool EnumProc(IntPtr h,IntPtr l);
  [StructLayout(LayoutKind.Sequential)] public struct Rect {public int left,top,right,bottom;}
  [StructLayout(LayoutKind.Sequential,CharSet=CharSet.Unicode)] struct MonitorInfo {public int size;public Rect monitor,work;public uint flags;}
  public class WindowInfo {public string className,title;public bool visible,enabled;public Rect bounds;}
  public class State {public bool foreground,foregroundOwn,dialogReady;public string foregroundClass,foregroundOwnerClass,reloadText,dialogMessage;public Rect shell,work;public List<WindowInfo> popups=new List<WindowInfo>();}
  [DllImport("user32.dll")] static extern bool EnumWindows(EnumProc p,IntPtr l);
  [DllImport("user32.dll")] static extern bool EnumChildWindows(IntPtr h,EnumProc p,IntPtr l);
  [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr h,out uint p);
  [DllImport("user32.dll",CharSet=CharSet.Unicode)] static extern int GetClassName(IntPtr h,StringBuilder s,int n);
  [DllImport("user32.dll",CharSet=CharSet.Unicode)] static extern int GetWindowText(IntPtr h,StringBuilder s,int n);
  [DllImport("user32.dll")] static extern bool GetWindowRect(IntPtr h,out Rect r);
  [DllImport("user32.dll")] static extern IntPtr MonitorFromWindow(IntPtr h,uint flags);
  [DllImport("user32.dll",CharSet=CharSet.Unicode)] static extern bool GetMonitorInfo(IntPtr h,ref MonitorInfo info);
  [DllImport("user32.dll")] static extern IntPtr GetDlgItem(IntPtr h,int id);
  [DllImport("user32.dll")] static extern int GetDlgCtrlID(IntPtr h);
  [DllImport("user32.dll")] static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] static extern bool IsWindowEnabled(IntPtr h);
  [DllImport("user32.dll")] static extern IntPtr GetForegroundWindow();
  [DllImport("user32.dll")] static extern IntPtr GetAncestor(IntPtr h,uint flag);
  [DllImport("user32.dll")] static extern IntPtr GetFocus();
  [DllImport("user32.dll")] static extern IntPtr SetFocus(IntPtr h);
  [DllImport("user32.dll")] static extern bool AttachThreadInput(uint a,uint b,bool attach);
  [DllImport("kernel32.dll")] static extern uint GetCurrentThreadId();
  [DllImport("user32.dll")] static extern bool SetWindowPos(IntPtr h,IntPtr after,int x,int y,int w,int height,uint flags);
  [DllImport("user32.dll")] static extern IntPtr SendMessageTimeout(IntPtr h,uint m,IntPtr w,IntPtr l,uint f,uint t,out IntPtr r);
  [DllImport("user32.dll",CharSet=CharSet.Unicode)] static extern IntPtr SendMessageTimeout(IntPtr h,uint m,IntPtr w,StringBuilder l,uint f,uint t,out IntPtr r);
  [DllImport("user32.dll")] static extern bool PostMessage(IntPtr h,uint m,IntPtr w,IntPtr l);
  [DllImport("user32.dll")] public static extern bool SetProcessDpiAwarenessContext(IntPtr c);
  static string Class(IntPtr h){var s=new StringBuilder(128);GetClassName(h,s,128);return s.ToString();}
  static string Text(IntPtr h){var s=new StringBuilder(2048);GetWindowText(h,s,2048);return s.ToString();}
  static string ControlText(IntPtr h){var s=new StringBuilder(2048);IntPtr r;if(SendMessageTimeout(h,0x0d,(IntPtr)s.Capacity,s,2,3000,out r)==IntPtr.Zero)throw new Exception("Fixture text read timed out");return s.ToString();}
  static void Send(IntPtr h,uint m,IntPtr w,IntPtr l){IntPtr r;if(SendMessageTimeout(h,m,w,l,2,3000,out r)==IntPtr.Zero)throw new Exception("Fixture native message timed out");}
  public static State Run(uint pid,string action,int x,int y,int width,int height){
    IntPtr shell=IntPtr.Zero,controls=IntPtr.Zero,source=IntPtr.Zero,renderer=IntPtr.Zero;
    var state=new State();
    EnumWindows((h,l)=>{uint p;GetWindowThreadProcessId(h,out p);if(p==pid){var cls=Class(h);if(cls=="XenonBrowserShell")shell=h;if(cls=="XenonControlCenter")controls=h;if(cls.StartsWith("Chrome_WidgetWin")||cls=="#32770"){Rect r;GetWindowRect(h,out r);state.popups.Add(new WindowInfo{className=cls,title=Text(h),visible=IsWindowVisible(h),enabled=IsWindowEnabled(h),bounds=r});}}return true;},IntPtr.Zero);
    if(shell==IntPtr.Zero)throw new Exception("Synthetic shell missing");
    var monitor=new MonitorInfo();monitor.size=Marshal.SizeOf(monitor);
    if(!GetMonitorInfo(MonitorFromWindow(shell,2),ref monitor))throw new Exception("Fixture monitor unavailable");state.work=monitor.work;
    if(action=="place"&&!SetWindowPos(shell,IntPtr.Zero,state.work.left+x,state.work.top+y,width,height,0x14))throw new Exception("Fixture placement failed");
    // Covered tabs remain visible behind the selected host. Window enumeration
    // is in Z order: use the first host, then its first renderer, never the last
    // visible renderer from an arbitrary covered tab.
    EnumChildWindows(shell,(h,l)=>{if(source==IntPtr.Zero&&Class(h)=="XenonTabHost"&&IsWindowVisible(h))source=h;return true;},IntPtr.Zero);
    if(source!=IntPtr.Zero)EnumChildWindows(source,(h,l)=>{if(renderer==IntPtr.Zero&&Class(h)=="Chrome_RenderWidgetHostHWND"&&IsWindowVisible(h))renderer=h;return true;},IntPtr.Zero);
    if(action=="reload")Send(shell,0x111,(IntPtr)2003,IntPtr.Zero);
    if(action=="click"||action=="dropdown"){
      if(renderer==IntPtr.Zero)throw new Exception("Synthetic renderer missing");
      if(GetAncestor(GetForegroundWindow(),3)==shell){
        uint p;var thread=GetWindowThreadProcessId(renderer,out p);var current=GetCurrentThreadId();
        var attached=AttachThreadInput(current,thread,true);
        try{SetFocus(renderer);if(GetFocus()!=renderer)throw new Exception("Synthetic page focus denied");}
        finally{if(attached)AttachThreadInput(current,thread,false);}
      }
      var point=(IntPtr)((y<<16)|(x&0xffff));Send(renderer,0x201,(IntPtr)1,point);Send(renderer,0x202,IntPtr.Zero,point);
    }
    if(action=="dropdown"){
      for(int attempt=0;attempt<20;attempt++){
        System.Threading.Thread.Sleep(100);state.popups.Clear();
        EnumWindows((h,l)=>{uint p;GetWindowThreadProcessId(h,out p);if(p==pid&&Class(h).StartsWith("Chrome_WidgetWin")){Rect r;GetWindowRect(h,out r);state.popups.Add(new WindowInfo{className=Class(h),title=Text(h),visible=IsWindowVisible(h),enabled=IsWindowEnabled(h),bounds=r});}return true;},IntPtr.Zero);
        if(state.popups.Exists(w=>w.visible&&w.className=="Chrome_WidgetWin_1"))break;
      }
      if(state.popups.Exists(w=>w.visible&&w.className=="Chrome_WidgetWin_1")){
        // Keep opening, measurement and keyboard selection in one foreground
        // helper call. Launching another process between these can legitimately
        // dismiss the popup when the interactive desktop changes foreground.
        SendKey(renderer,0x28);System.Threading.Thread.Sleep(150);
        SendKey(renderer,0x0d);System.Threading.Thread.Sleep(150);
      }
    }
    if(action=="f5"){
      if(renderer==IntPtr.Zero)throw new Exception("Synthetic renderer missing");
      SendKey(renderer,0x74);
    }
    IntPtr accept=IntPtr.Zero,dismiss=IntPtr.Zero,message=IntPtr.Zero;
    if(controls!=IntPtr.Zero){
      EnumChildWindows(controls,(h,l)=>{if(Class(h).Equals("Button",StringComparison.OrdinalIgnoreCase)){var caption=Text(h);if(caption=="Accept dialog")accept=h;if(caption=="Dismiss")dismiss=h;}return true;},IntPtr.Zero);
      // DialogMessage in native_ui.cpp. Read only this synthetic dialog edit;
      // never enumerate the vault's username/password or other edit values.
      message=GetDlgItem(controls,142);
    }
    state.dialogReady=controls!=IntPtr.Zero&&IsWindowVisible(controls)&&accept!=IntPtr.Zero&&IsWindowVisible(accept)&&IsWindowEnabled(accept);
    if(state.dialogReady&&message!=IntPtr.Zero)state.dialogMessage=ControlText(message);
    if(action=="accept"||action=="dismiss"){
      var button=action=="accept"?accept:dismiss;
      if(!state.dialogReady||button==IntPtr.Zero||!IsWindowEnabled(button))throw new Exception("Synthetic dialog is not answerable");
      Send(controls,0x111,(IntPtr)GetDlgCtrlID(button),button);
    }
    GetWindowRect(shell,out state.shell);state.reloadText=Text(GetDlgItem(shell,2003));var foreground=GetForegroundWindow();uint foregroundPid;GetWindowThreadProcessId(foreground,out foregroundPid);state.foregroundOwn=foregroundPid==pid;state.foreground=GetAncestor(foreground,3)==shell;state.foregroundClass=Class(foreground);state.foregroundOwnerClass=Class(GetAncestor(foreground,3));
    return state;
  }
  static void SendKey(IntPtr renderer,int key){
    if(!PostMessage(renderer,0x100,(IntPtr)key,(IntPtr)1)||!PostMessage(renderer,0x101,(IntPtr)key,(IntPtr)0xc0000001))throw new Exception("Fixture key failed");
  }
}
'@
[void][NativePageControls]::SetProcessDpiAwarenessContext([IntPtr](-4))
if ($Action -in @('foreground','click','dropdown')) {
  & (Join-Path $PSScriptRoot 'popup-windows.ps1') -ProcessId $BrowserPid -Action foreground | Out-Null
}
[NativePageControls]::Run([uint32]$BrowserPid,$Action,$X,$Y,$Width,$Height) | ConvertTo-Json -Depth 6 -Compress
