param([string[]]$Titles=@('BLTest - Unreal Editor','Content Browser'),[int]$X=1124,[int]$Y=685,[int]$W=1353,[int]$H=784,[switch]$Max,[switch]$Restore)
Add-Type @"
using System;using System.Runtime.InteropServices;using System.Text;
public class X{public delegate bool EP(IntPtr h,IntPtr l);
[DllImport("user32.dll")]public static extern bool EnumWindows(EP f,IntPtr l);
[DllImport("user32.dll")]public static extern bool IsWindowVisible(IntPtr h);
[DllImport("user32.dll")]public static extern int GetWindowText(IntPtr h,StringBuilder s,int n);
[DllImport("user32.dll")]public static extern uint GetWindowThreadProcessId(IntPtr h,out uint p);
[DllImport("user32.dll")]public static extern bool SetWindowPos(IntPtr h,IntPtr a,int x,int y,int w,int c,uint f);
[DllImport("user32.dll")]public static extern bool ShowWindow(IntPtr h,int n);
[DllImport("user32.dll")]public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr c);}
"@
[X]::SetThreadDpiAwarenessContext([IntPtr]-4)|Out-Null
$ids=(Get-Process UnrealEditor).Id; $found=@{}
$cb=[X+EP]{param($h,$l) $p=0;[X]::GetWindowThreadProcessId($h,[ref]$p)|Out-Null; if($ids -contains $p -and [X]::IsWindowVisible($h)){$sb=New-Object Text.StringBuilder 256;[X]::GetWindowText($h,$sb,256)|Out-Null; if($Titles -contains $sb.ToString()){$found[$sb.ToString()]=$h}}; $true}
[X]::EnumWindows($cb,[IntPtr]::Zero)|Out-Null
foreach($t in $Titles){$h=$found[$t]; if(!$h){"missing $t";continue}
 if($Restore){[X]::ShowWindow($h,9)|Out-Null}
 else{[X]::ShowWindow($h,9)|Out-Null;[X]::SetWindowPos($h,[IntPtr]::Zero,$X,$Y,$W,$H,0x14)|Out-Null; if($Max){[X]::ShowWindow($h,3)|Out-Null}}
 "done $t $h"}
