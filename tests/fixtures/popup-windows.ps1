param([Parameter(Mandatory=$true)][int]$ProcessId,[ValidateSet('snapshot','foreground','minimize','focus-page','cover')][string]$Action='snapshot')
# Read only the synthetic browser's top-level window metadata.
$ErrorActionPreference = 'Stop'
Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;
public static class PopupWindows {
  delegate bool EnumProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] static extern bool EnumWindows(EnumProc f, IntPtr l);
  [DllImport("user32.dll")] static extern bool EnumChildWindows(IntPtr h,EnumProc f,IntPtr l);
  [DllImport("user32.dll")] static extern IntPtr SetFocus(IntPtr h);
  [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] static extern int GetClassNameW(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll",CharSet=CharSet.Unicode)] static extern IntPtr CreateWindowExW(uint ex,string cls,string title,uint style,int x,int y,int w,int h,IntPtr parent,IntPtr menu,IntPtr instance,IntPtr data);
  [DllImport("user32.dll")] static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] static extern IntPtr GetWindow(IntPtr h, uint command);
  [DllImport("user32.dll")] static extern IntPtr GetForegroundWindow();
  [DllImport("user32.dll")] static extern IntPtr GetParent(IntPtr h);
  [DllImport("user32.dll")] static extern bool ShowWindow(IntPtr h,int command);
  [DllImport("user32.dll")] static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] static extern bool AttachThreadInput(uint a,uint b,bool attach);
  [DllImport("kernel32.dll")] static extern uint GetCurrentThreadId();
  [DllImport("user32.dll")] static extern bool GetGUIThreadInfo(uint thread,ref GuiInfo info);
  [StructLayout(LayoutKind.Sequential)] struct Rect {public int l,t,r,b;}
  [StructLayout(LayoutKind.Sequential)] struct GuiInfo {public int size,flags;public IntPtr active,focus,capture,menuOwner,moveSize,caret;public Rect rect;}
  static string Class(IntPtr h) {var s=new StringBuilder(256);GetClassNameW(h,s,256);return s.ToString();}
  public static string Foreground(){return Class(GetForegroundWindow());}
  public static bool ForegroundOwn(uint pid){uint p;GetWindowThreadProcessId(GetForegroundWindow(),out p);return p==pid;}
  public static void Action(uint pid,string action) {
    IntPtr shell=IntPtr.Zero;
    EnumWindows((h,l)=>{uint p;GetWindowThreadProcessId(h,out p);if(p==pid&&Class(h)=="XenonBrowserShell"){shell=h;return false;}return true;},IntPtr.Zero);
    if(shell==IntPtr.Zero)throw new Exception("Synthetic browser missing");
    if(action=="focus-page") {
      IntPtr source=IntPtr.Zero,widget=IntPtr.Zero;
      // The selected tab host is the first host in the shell's child Z order.
      for(var h=GetWindow(shell,5);h!=IntPtr.Zero;h=GetWindow(h,2))if(Class(h)=="XenonTabHost"){source=h;break;}
      if(source!=IntPtr.Zero)EnumChildWindows(source,(h,l)=>{if(Class(h)=="CefBrowserWindow"){widget=h;return false;}return true;},IntPtr.Zero);
      if(widget==IntPtr.Zero)throw new Exception("Synthetic selected page widget missing");
      uint p;var thread=GetWindowThreadProcessId(widget,out p);var current=GetCurrentThreadId();
      var attached=AttachThreadInput(current,thread,true);
      try{SetFocus(widget);}finally{if(attached)AttachThreadInput(current,thread,false);}
    }
    if(action=="minimize")ShowWindow(shell,6);
    if(action=="cover"){
      shell=CreateWindowExW(0,"STATIC","Synthetic popup occluder",0x00CF0000,40,40,320,180,IntPtr.Zero,IntPtr.Zero,IntPtr.Zero,IntPtr.Zero);
      if(shell==IntPtr.Zero)throw new Exception("Synthetic cover window creation failed");
    }
    if(action=="foreground"||action=="cover"){
      ShowWindow(shell,9);SetForegroundWindow(shell);
      for(int i=0;i<10 && GetForegroundWindow()!=shell;i++){
        uint p;var foregroundThread=GetWindowThreadProcessId(GetForegroundWindow(),out p);var thread=GetWindowThreadProcessId(shell,out p);var current=GetCurrentThreadId();
        var toForeground=AttachThreadInput(current,foregroundThread,true);
        var toBrowser=AttachThreadInput(current,thread,true);
        try{SetForegroundWindow(shell);}finally{if(toBrowser)AttachThreadInput(current,thread,false);if(toForeground)AttachThreadInput(current,foregroundThread,false);}
        System.Threading.Thread.Sleep(100);
      }
      if(GetForegroundWindow()!=shell)throw new Exception("Synthetic foreground denied");
      if(action=="cover")System.Threading.Thread.Sleep(500);
    }
  }
  public static string Focus(uint pid) {
    string result="";
    EnumWindows((h,l)=>{uint p;var thread=GetWindowThreadProcessId(h,out p);if(p==pid){var info=new GuiInfo();info.size=Marshal.SizeOf(info);if(GetGUIThreadInfo(thread,ref info)&&info.focus!=IntPtr.Zero)result=Class(info.focus);}return true;},IntPtr.Zero);
    return result;
  }
  public static string Capture(uint pid) {
    string result="";
    EnumWindows((h,l)=>{uint p;var thread=GetWindowThreadProcessId(h,out p);if(p==pid){var info=new GuiInfo();info.size=Marshal.SizeOf(info);if(GetGUIThreadInfo(thread,ref info)&&info.capture!=IntPtr.Zero)result=Class(info.capture);}return true;},IntPtr.Zero);
    return result;
  }
  public static string[] Visible(uint pid) {
    var result = new List<string>();
    EnumWindows((h,l) => {
      uint p; GetWindowThreadProcessId(h,out p);
      if(p==pid && IsWindowVisible(h)) {
        var s=new StringBuilder(256);GetClassNameW(h,s,256);
        var chain=s.ToString();var parent=GetWindow(h,4);
        for(int i=0;i<8 && parent!=IntPtr.Zero;i++){chain+=" > "+Class(parent);parent=GetParent(parent);}
        result.Add(chain);
      }
      return true;
    },IntPtr.Zero);
    return result.ToArray();
  }
}
'@
[PopupWindows]::Action([uint32]$ProcessId,$Action)
@{windows=@([PopupWindows]::Visible([uint32]$ProcessId));focus=[PopupWindows]::Focus([uint32]$ProcessId);capture=[PopupWindows]::Capture([uint32]$ProcessId);foreground=[PopupWindows]::Foreground();foregroundOwn=[PopupWindows]::ForegroundOwn([uint32]$ProcessId)}|ConvertTo-Json -Compress
