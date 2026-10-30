param([string]$Tag,[int]$Seconds=25,[string]$Action='')
$d='C:\Users\Revaptor_FX\BongLodge_src\tests'
Add-Type @"
using System;using System.Runtime.InteropServices;
public class C{[DllImport("user32.dll")]public static extern bool PostMessage(IntPtr h,uint m,IntPtr w,IntPtr l);
[DllImport("user32.dll")]public static extern bool ShowWindow(IntPtr h,int c);
[DllImport("user32.dll")]public static extern bool SetWindowPos(IntPtr h,IntPtr a,int x,int y,int cx,int cy,uint f);}
"@
& "$d\rectpoll.ps1" -Seconds $Seconds -Out "$d\r4_$Tag.txt" -Launch | Out-Null
$p=Get-Process UnrealEditor|?{$_.MainWindowTitle -like 'BLTest*'}|select -f 1
if($Action -eq 'max'){[C]::ShowWindow($p.MainWindowHandle,3)|Out-Null; Start-Sleep 3}
if($Action -eq 'restore'){[C]::ShowWindow($p.MainWindowHandle,9)|Out-Null; Start-Sleep 3}
[C]::PostMessage($p.MainWindowHandle,0x10,0,0)|Out-Null
if(-not $p.WaitForExit(40000)){"NOT EXITED"}else{"exited"}
gc "$d\r4_$Tag.txt"
