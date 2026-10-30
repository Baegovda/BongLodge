param([string]$Out)
Add-Type @"
using System;using System.Runtime.InteropServices;
public class M{[DllImport("user32.dll")]public static extern bool SetCursorPos(int x,int y);
[DllImport("user32.dll")]public static extern void mouse_event(uint f,int x,int y,int d,IntPtr e);
[DllImport("user32.dll")]public static extern bool SetForegroundWindow(IntPtr h);
[DllImport("user32.dll")]public static extern bool SetProcessDPIAware();}
"@
[M]::SetProcessDPIAware()|Out-Null
$p=Get-Process UnrealEditor|?{$_.MainWindowTitle -like 'BLTest*'}|select -f 1
InputBlockOn
[M]::SetForegroundWindow($p.MainWindowHandle)|Out-Null; Start-Sleep -m 800
$x0=3214;$y0=1114;$x1=2000;$y1=1500
[M]::SetCursorPos($x0,$y0); Start-Sleep -m 300; [M]::mouse_event(2,0,0,0,0); Start-Sleep -m 200
for($i=1;$i -le 30;$i++){[M]::SetCursorPos([int]($x0+($x1-$x0)*$i/30),[int]($y0+($y1-$y0)*$i/30)); Start-Sleep -m 30}
Start-Sleep -m 400; [M]::mouse_event(4,0,0,0,0); Start-Sleep -m 500
InputBlockOff
