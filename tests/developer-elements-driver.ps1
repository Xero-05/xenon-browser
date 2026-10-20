# Native message driver scoped to the harness's disposable PID.
# This is test coverage, not physical input or a shipped browser capability.
param([int]$BrowserPid,[string]$Action='state',[string]$Name,[string]$Image)
$ErrorActionPreference='Stop'
[Console]::OutputEncoding=New-Object Text.UTF8Encoding $false
Add-Type -AssemblyName System.Drawing
Add-Type -TypeDefinition @'
using System; using System.Text; using System.Runtime.InteropServices;
public static class DeveloperElementsDriver {
 delegate bool EnumProc(IntPtr h,IntPtr p);
 [DllImport("user32.dll")] static extern bool EnumWindows(EnumProc cb,IntPtr p);
 [DllImport("user32.dll")] static extern bool EnumChildWindows(IntPtr parent,EnumProc cb,IntPtr p);
 [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr h,out uint p);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] static extern int GetClassName(IntPtr h,StringBuilder s,int n);
 [DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr h,int id);
 [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr h,uint m,IntPtr w,IntPtr l);
 [DllImport("user32.dll",SetLastError=true)] static extern IntPtr SendMessageTimeout(IntPtr h,uint m,IntPtr w,IntPtr l,uint flags,uint timeout,out IntPtr result);
 public static IntPtr BoundedMessage(IntPtr h,uint m,IntPtr w,IntPtr l){IntPtr result;if(SendMessageTimeout(h,m,w,l,2,3000,out result)==IntPtr.Zero)throw new InvalidOperationException("Synthetic focus message timed out: "+m);return result;}
 [DllImport("user32.dll",CharSet=CharSet.Unicode,EntryPoint="SendMessageW")] static extern IntPtr TextMessage(IntPtr h,uint m,IntPtr w,StringBuilder l);
 [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h,uint m,IntPtr w,IntPtr l);
 [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
 [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h,IntPtr dc,uint flags);
 [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h,out Rect r);
 [DllImport("user32.dll")] public static extern int MapWindowPoints(IntPtr from,IntPtr to,ref Rect r,uint count);
 [DllImport("user32.dll")] public static extern bool SetCursorPos(int x,int y);
 [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
 [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
 [DllImport("user32.dll")] public static extern IntPtr WindowFromPoint(Point p);
 [DllImport("user32.dll")] public static extern bool GetCursorPos(out Point p);
 public struct Point {public int x,y;}
 public static string Class(IntPtr h){var s=new StringBuilder(128);GetClassName(h,s,128);return s.ToString();}
 public struct Rect { public int left,top,right,bottom; }
 public static IntPtr Find(uint pid,string cls){IntPtr result=IntPtr.Zero;EnumWindows((h,p)=>{uint owner;GetWindowThreadProcessId(h,out owner);var s=new StringBuilder(128);GetClassName(h,s,128);if(owner==pid&&s.ToString()==cls){result=h;return false;}return true;},IntPtr.Zero);return result;}
 public static IntPtr Child(IntPtr root,string cls){IntPtr result=IntPtr.Zero;EnumChildWindows(root,(h,p)=>{var s=new StringBuilder(128);GetClassName(h,s,128);if(s.ToString()==cls){result=h;return false;}return true;},IntPtr.Zero);return result;}
 public static IntPtr VisibleChild(IntPtr root,string cls){IntPtr result=IntPtr.Zero;EnumChildWindows(root,(h,p)=>{var s=new StringBuilder(128);GetClassName(h,s,128);if(IsWindowVisible(h)&&s.ToString()==cls){result=h;return false;}return true;},IntPtr.Zero);return result;}
 public static string Classes(IntPtr root){var result=new StringBuilder();EnumChildWindows(root,(h,p)=>{var s=new StringBuilder(128);GetClassName(h,s,128);if(IsWindowVisible(h))result.Append(s.ToString()).Append("\n");return true;},IntPtr.Zero);return result.ToString();}
 [DllImport("user32.dll")] public static extern uint GetDpiForWindow(IntPtr h);
 [DllImport("user32.dll")] public static extern uint GetSysColor(int index);
 public static string Text(IntPtr h){var n=SendMessage(h,14,IntPtr.Zero,IntPtr.Zero).ToInt32();var s=new StringBuilder(n+1);TextMessage(h,13,(IntPtr)(n+1),s);return s.ToString();}
}
'@
$shell=[DeveloperElementsDriver]::Find($BrowserPid,'XenonBrowserShell')
$controls=[DeveloperElementsDriver]::Find($BrowserPid,'XenonControlCenter')
if($shell -eq [IntPtr]::Zero){throw 'Disposable shell missing'}
if($Action -eq 'prepare'){
 # Capture-based divider drags require a foreground window. Keep the real
 # stationary pointer off the page so passive hover cannot overwrite the
 # native-message hover positions used by this fixture.
 [void][DeveloperElementsDriver]::SetForegroundWindow($shell)
 $prepareRect=New-Object DeveloperElementsDriver+Rect
 [void][DeveloperElementsDriver]::GetWindowRect($shell,[ref]$prepareRect)
 if(-not [DeveloperElementsDriver]::SetCursorPos($prepareRect.left+30,$prepareRect.top+15)){throw 'Could not park the fixture pointer'}
 if([DeveloperElementsDriver]::GetForegroundWindow() -ne $shell){throw 'Could not foreground the disposable fixture'}
 @{prepared=$true}|ConvertTo-Json -Compress;return
}
if($Action -eq 'classes'){[DeveloperElementsDriver]::Classes($shell);return}
if($Action -eq 'cursor-state'){$cursorPoint=New-Object DeveloperElementsDriver+Point;[void][DeveloperElementsDriver]::GetCursorPos([ref]$cursorPoint);@{foregroundOwn=[DeveloperElementsDriver]::GetForegroundWindow() -eq $shell;underClass=[DeveloperElementsDriver]::Class([DeveloperElementsDriver]::WindowFromPoint($cursorPoint));x=$cursorPoint.x;y=$cursorPoint.y}|ConvertTo-Json -Compress;return}
if($Action -eq 'close'){[void][DeveloperElementsDriver]::PostMessage($shell,16,[IntPtr]::Zero,[IntPtr]::Zero);return}
if($Action -in @('collection-toggle','collection-clear','collection-state')){
 if($controls -eq [IntPtr]::Zero){throw 'Disposable Controls missing'}
 if($Action -eq 'collection-toggle'){[void][DeveloperElementsDriver]::SendMessage($controls,273,[IntPtr]159,[IntPtr]::Zero)}
 if($Action -eq 'collection-clear'){[void][DeveloperElementsDriver]::SendMessage($controls,273,[IntPtr]161,[IntPtr]::Zero)}
 if($Image){
  $captureRect=New-Object DeveloperElementsDriver+Rect;[void][DeveloperElementsDriver]::GetWindowRect($controls,[ref]$captureRect)
  $capture=New-Object Drawing.Bitmap ($captureRect.right-$captureRect.left),($captureRect.bottom-$captureRect.top);$captureGraphics=[Drawing.Graphics]::FromImage($capture);$captureDc=$captureGraphics.GetHdc()
  try{if(-not [DeveloperElementsDriver]::PrintWindow($controls,$captureDc,2)){throw 'Collection Controls PrintWindow failed'}}finally{$captureGraphics.ReleaseHdc($captureDc);$captureGraphics.Dispose()}
  $capture.Save($Image,[Drawing.Imaging.ImageFormat]::Png);$capture.Dispose()
 }
 @{toggleText=[DeveloperElementsDriver]::Text([DeveloperElementsDriver]::GetDlgItem($controls,159));statusText=[DeveloperElementsDriver]::Text([DeveloperElementsDriver]::GetDlgItem($controls,162))}|ConvertTo-Json -Compress;return
}
if($Action -eq 'maximize'){[void][DeveloperElementsDriver]::SendMessage($shell,274,[IntPtr]61488,[IntPtr]::Zero)}
if($Action -eq 'open'){[void][DeveloperElementsDriver]::SendMessage($shell,273,[IntPtr]2125,[IntPtr]::Zero)}
if($Action -eq 'toggle'){if($controls -eq [IntPtr]::Zero){throw 'Disposable Controls missing'};[void][DeveloperElementsDriver]::SendMessage($controls,273,[IntPtr]154,[IntPtr]::Zero)}
if($Action -eq 'select-tab'){
 $tree=[DeveloperElementsDriver]::GetDlgItem($shell,2009)
 $group=[DeveloperElementsDriver]::SendMessage($tree,4362,[IntPtr]::Zero,[IntPtr]::Zero)
 $tab=[DeveloperElementsDriver]::SendMessage($tree,4362,[IntPtr]4,$group)
 $index=if($Name -eq 'Second elements fixture'){2}else{1}
 for($i=0;$i -lt $index;$i++){$tab=[DeveloperElementsDriver]::SendMessage($tree,4362,[IntPtr]1,$tab)}
 if($tab -eq [IntPtr]::Zero){throw 'Synthetic tab position missing'}
 [void][DeveloperElementsDriver]::SendMessage($tree,4363,[IntPtr]9,$tab)
}
$panel=[DeveloperElementsDriver]::Child($shell,'XenonElementsPanel');if($panel -eq [IntPtr]::Zero){throw 'Elements panel missing'}
$listHandle=[DeveloperElementsDriver]::GetDlgItem($panel,2803)
$visible=[DeveloperElementsDriver]::IsWindowVisible($panel)
if($Action -eq 'tab-focus-cues'){
 $tree=[DeveloperElementsDriver]::GetDlgItem($shell,2009)
 $selected=[DeveloperElementsDriver]::BoundedMessage($tree,4362,[IntPtr]9,[IntPtr]::Zero)
 [void][DeveloperElementsDriver]::BoundedMessage($tree,256,[IntPtr]17,[IntPtr]::Zero)
 $keyboardBefore=[DeveloperElementsDriver]::BoundedMessage($tree,297,[IntPtr]::Zero,[IntPtr]::Zero).ToInt32()
 $height=[DeveloperElementsDriver]::BoundedMessage($tree,4380,[IntPtr]::Zero,[IntPtr]::Zero).ToInt32()
 $point=[IntPtr](([int]($height*2.5) -shl 16) -bor 60)
 # TreeView mouse-down can wait inside Windows' drag detection. Queue both
 # halves so mouse-up can drain while that native loop is active.
 [void][DeveloperElementsDriver]::PostMessage($tree,513,[IntPtr]1,$point)
 [void][DeveloperElementsDriver]::PostMessage($tree,514,[IntPtr]::Zero,$point)
 Start-Sleep -Milliseconds 150
 $mouse=[DeveloperElementsDriver]::BoundedMessage($tree,297,[IntPtr]::Zero,[IntPtr]::Zero).ToInt32()
 $same=$selected -eq [DeveloperElementsDriver]::BoundedMessage($tree,4362,[IntPtr]9,[IntPtr]::Zero)
 [void][DeveloperElementsDriver]::BoundedMessage($tree,256,[IntPtr]17,[IntPtr]::Zero)
 $keyboardAfter=[DeveloperElementsDriver]::BoundedMessage($tree,297,[IntPtr]::Zero,[IntPtr]::Zero).ToInt32()
 @{keyboardBefore=$keyboardBefore;mouse=$mouse;keyboardAfter=$keyboardAfter;sameSelectedTab=$same}|ConvertTo-Json -Compress;exit
}
if($Action -eq 'selection-colors'){
 $rect=New-Object DeveloperElementsDriver+Rect;[void][DeveloperElementsDriver]::GetWindowRect($listHandle,[ref]$rect)
 $bitmap=New-Object Drawing.Bitmap ($rect.right-$rect.left),($rect.bottom-$rect.top);$graphics=[Drawing.Graphics]::FromImage($bitmap);$dc=$graphics.GetHdc()
 try{if(-not [DeveloperElementsDriver]::PrintWindow($listHandle,$dc,2)){throw 'Element list PrintWindow failed'}}finally{$graphics.ReleaseHdc($dc);$graphics.Dispose()}
 $rgb=$Name.Split(',');$expected=[Drawing.Color]::FromArgb([int]$rgb[0],[int]$rgb[1],[int]$rgb[2]).ToArgb()
 $system=[DeveloperElementsDriver]::GetSysColor(13);$highlight=[Drawing.Color]::FromArgb([int]($system -band 255),[int](($system -shr 8) -band 255),[int](($system -shr 16) -band 255)).ToArgb()
 $themed=0;$default=0
 for($y=0;$y -lt $bitmap.Height;$y++){for($x=0;$x -lt $bitmap.Width;$x++){ $pixel=$bitmap.GetPixel($x,$y).ToArgb();if($pixel -eq $expected){$themed++};if($pixel -eq $highlight){$default++} }}
 if($Image){$bitmap.Save($Image,[Drawing.Imaging.ImageFormat]::Png)};$bitmap.Dispose()
 @{themedSelectionPixels=$themed;systemHighlightPixels=$default}|ConvertTo-Json -Compress;exit
}
if($Action -in @('resize-width','resize-split')){
 $rect=New-Object DeveloperElementsDriver+Rect
 $delta=[int]$Name
 if($Action -eq 'resize-width'){
  [void][DeveloperElementsDriver]::GetWindowRect($panel,[ref]$rect);[void][DeveloperElementsDriver]::MapWindowPoints([IntPtr]::Zero,$shell,[ref]$rect,2)
  $target=$shell;$x=$rect.left-4;$y=$rect.top+160;$endX=$x-$delta;$endY=$y
 }else{
  [void][DeveloperElementsDriver]::GetWindowRect([DeveloperElementsDriver]::GetDlgItem($panel,2805),[ref]$rect);[void][DeveloperElementsDriver]::MapWindowPoints([IntPtr]::Zero,$panel,[ref]$rect,2)
  $target=$panel;$x=$rect.left+30;$y=$rect.top-[int](5*[DeveloperElementsDriver]::GetDpiForWindow($panel)/96);$endX=$x;$endY=$y+$delta
 }
 $start=[IntPtr](($y -shl 16) -bor ($x -band 65535));$end=[IntPtr](($endY -shl 16) -bor ($endX -band 65535))
 # Windows sends real pointer positions during capture even for a message-
 # driven drag. Align that position with the fixture gesture so those messages
 # cannot undo the requested divider size between cross-process calls.
 $cursorRect=New-Object DeveloperElementsDriver+Rect
 $cursorRect.left=$x;$cursorRect.top=$y;$cursorRect.right=$endX;$cursorRect.bottom=$endY
 [void][DeveloperElementsDriver]::MapWindowPoints($target,[IntPtr]::Zero,[ref]$cursorRect,2)
 if(-not [DeveloperElementsDriver]::SetCursorPos($cursorRect.left,$cursorRect.top)){throw 'Could not position the fixture drag'}
 [void][DeveloperElementsDriver]::SendMessage($target,513,[IntPtr]1,$start)
 if(-not [DeveloperElementsDriver]::SetCursorPos($cursorRect.right,$cursorRect.bottom)){throw 'Could not position the fixture drag endpoint'}
 [void][DeveloperElementsDriver]::SendMessage($target,512,[IntPtr]1,$end)
 [void][DeveloperElementsDriver]::SendMessage($target,514,[IntPtr]::Zero,$end)
 $parkRect=New-Object DeveloperElementsDriver+Rect;[void][DeveloperElementsDriver]::GetWindowRect($shell,[ref]$parkRect)
 if(-not [DeveloperElementsDriver]::SetCursorPos($parkRect.left+30,$parkRect.top+15)){throw 'Could not park the fixture pointer after resize'}
}
if($Action -eq 'filter'){[void][DeveloperElementsDriver]::SendMessage([DeveloperElementsDriver]::GetDlgItem($panel,2802),245,[IntPtr]::Zero,[IntPtr]::Zero)}
if($Action -eq 'pick'){[void][DeveloperElementsDriver]::SendMessage([DeveloperElementsDriver]::GetDlgItem($panel,2806),245,[IntPtr]::Zero,[IntPtr]::Zero)}
if($Action -in @('page-pick','page-hover')){
 $node=$Name|ConvertFrom-Json
 $page=if($Action -eq 'page-pick'){[DeveloperElementsDriver]::VisibleChild($shell,'XenonElementOverlay')}else{[DeveloperElementsDriver]::VisibleChild($shell,'CefBrowserWindow')};if($page -eq [IntPtr]::Zero){throw 'Native page surface missing'}
 # Client pixels per viewport CSS pixel; a pinch magnifies them, so the pinch suite passes clientScale.
 $scale=if($node.clientScale){[double]$node.clientScale}else{[DeveloperElementsDriver]::GetDpiForWindow($page)/96.0}
 $x=[int](($node.bounds.x+$node.bounds.width/2-$node.viewport.pageX)*$scale);$y=[int](($node.bounds.y+$node.bounds.height/2-$node.viewport.pageY)*$scale)
 $position=[IntPtr](($y -shl 16) -bor ($x -band 65535))
 [void][DeveloperElementsDriver]::PostMessage($page,512,[IntPtr]::Zero,$position)
 if($Action -eq 'page-hover'){
  # Native messages test the production diagnostic path; this runner cannot
  # place the real desktop cursor or claim physical hover coverage.
  $surfaceRect=New-Object DeveloperElementsDriver+Rect;[void][DeveloperElementsDriver]::GetWindowRect($page,[ref]$surfaceRect);[void][DeveloperElementsDriver]::MapWindowPoints([IntPtr]::Zero,$shell,[ref]$surfaceRect,2)
  $hoverX=$surfaceRect.left+$x;$hoverY=$surfaceRect.top+$y;$hoverPosition=[IntPtr](($hoverY -shl 16) -bor ($hoverX -band 65535))
  [void][DeveloperElementsDriver]::PostMessage($shell,512,[IntPtr]::Zero,$hoverPosition);Start-Sleep -Milliseconds 400
 }else{
 Start-Sleep -Milliseconds 100
 [void][DeveloperElementsDriver]::PostMessage($page,513,[IntPtr]1,$position)
 [void][DeveloperElementsDriver]::PostMessage($page,514,[IntPtr]::Zero,$position)
 }
}
if($Action -eq 'scroll-details'){[void][DeveloperElementsDriver]::SendMessage([DeveloperElementsDriver]::GetDlgItem($panel,2805),182,[IntPtr]::Zero,[IntPtr]8)}
if($Action -in @('select-element','select-role','select-uncertain')){
 # Select using ordinary list keyboard messages; inspect the panel's own
 # read-only detail text, avoiding cross-process LVM pointer structures.
 [void][DeveloperElementsDriver]::SendMessage($listHandle,256,[IntPtr]36,[IntPtr]::Zero)
 $found=$false;$count=[DeveloperElementsDriver]::SendMessage($listHandle,4100,[IntPtr]::Zero,[IntPtr]::Zero).ToInt32()
 for($i=0;$i -lt $count;$i++){
  $text=[DeveloperElementsDriver]::Text([DeveloperElementsDriver]::GetDlgItem($panel,2805))
  if($text){$node=$text|ConvertFrom-Json;if(($Action -eq 'select-element' -and ($node.name -eq $Name -or $node.siteLabel.text -eq $Name)) -or ($Action -eq 'select-role' -and $node.role -eq $Name) -or ($Action -eq 'select-uncertain' -and $node.visibilityStatus -eq 'uncertain')){$found=$true;break}}
  [void][DeveloperElementsDriver]::SendMessage($listHandle,256,[IntPtr]40,[IntPtr]::Zero)
 }
 if(-not $found){throw 'Named rendered element missing'}
}
$labels=@();$nodes=@()
if($visible -and $Action -notin @('select-element','select-role','select-uncertain','selection-state','pick','page-pick','page-hover','scroll-details')){
 $selected=[DeveloperElementsDriver]::SendMessage($listHandle,4108,[IntPtr](-1),[IntPtr]2).ToInt32()
 [void][DeveloperElementsDriver]::SendMessage($listHandle,256,[IntPtr]36,[IntPtr]::Zero)
 $count=[DeveloperElementsDriver]::SendMessage($listHandle,4100,[IntPtr]::Zero,[IntPtr]::Zero).ToInt32()
 for($i=0;$i -lt $count;$i++){
  $text=[DeveloperElementsDriver]::Text([DeveloperElementsDriver]::GetDlgItem($panel,2805))
  if($text){$node=$text|ConvertFrom-Json;$labels+=$node.name;$nodes+=$node}
  [void][DeveloperElementsDriver]::SendMessage($listHandle,256,[IntPtr]40,[IntPtr]::Zero)
 }
 [void][DeveloperElementsDriver]::SendMessage($listHandle,256,[IntPtr]36,[IntPtr]::Zero)
 for($i=0;$i -lt $selected;$i++){[void][DeveloperElementsDriver]::SendMessage($listHandle,256,[IntPtr]40,[IntPtr]::Zero)}
}
if($Image){
 $rect=New-Object DeveloperElementsDriver+Rect;[void][DeveloperElementsDriver]::GetWindowRect($shell,[ref]$rect)
 $bitmap=New-Object Drawing.Bitmap ($rect.right-$rect.left),($rect.bottom-$rect.top);$graphics=[Drawing.Graphics]::FromImage($bitmap);$dc=$graphics.GetHdc()
 try{if(-not [DeveloperElementsDriver]::PrintWindow($shell,$dc,2)){throw 'Fixture PrintWindow failed'}}finally{$graphics.ReleaseHdc($dc);$graphics.Dispose()}
 $bitmap.Save($Image,[Drawing.Imaging.ImageFormat]::Png);$bitmap.Dispose()
}
$panelRect=New-Object DeveloperElementsDriver+Rect;$listRect=New-Object DeveloperElementsDriver+Rect;$detailsRect=New-Object DeveloperElementsDriver+Rect
[void][DeveloperElementsDriver]::GetWindowRect($panel,[ref]$panelRect);[void][DeveloperElementsDriver]::GetWindowRect($listHandle,[ref]$listRect);[void][DeveloperElementsDriver]::GetWindowRect([DeveloperElementsDriver]::GetDlgItem($panel,2805),[ref]$detailsRect)
@{detailsFirstLine=[DeveloperElementsDriver]::SendMessage([DeveloperElementsDriver]::GetDlgItem($panel,2805),206,[IntPtr]::Zero,[IntPtr]::Zero).ToInt32();visible=$visible;panelWidth=$panelRect.right-$panelRect.left;listHeight=$listRect.bottom-$listRect.top;detailsHeight=$detailsRect.bottom-$detailsRect.top;count=[DeveloperElementsDriver]::SendMessage($listHandle,4100,[IntPtr]::Zero,[IntPtr]::Zero).ToInt32();labels=@($labels);nodes=@($nodes);details=[DeveloperElementsDriver]::Text([DeveloperElementsDriver]::GetDlgItem($panel,2805));status=[DeveloperElementsDriver]::Text([DeveloperElementsDriver]::GetDlgItem($panel,2804))}|ConvertTo-Json -Compress -Depth 8
