param([int]$Seconds=70,[string]$Out,[switch]$Launch)
Add-Type @"
using System;using System.Runtime.InteropServices;using System.Text;
public class W{public delegate bool EP(IntPtr h,IntPtr l);
[DllImport("user32.dll")]public static extern bool EnumWindows(EP f,IntPtr l);
[DllImport("user32.dll")]public static extern uint GetWindowThreadProcessId(IntPtr h,out uint p);
[DllImport("user32.dll")]public static extern bool IsWindowVisible(IntPtr h);
[DllImport("user32.dll")]public static extern bool IsZoomed(IntPtr h);
[DllImport("user32.dll")]public static extern bool IsIconic(IntPtr h);
[DllImport("user32.dll")]public static extern int GetWindowText(IntPtr h,StringBuilder s,int n);
[DllImport("user32.dll")]public static extern bool GetWindowRect(IntPtr h,out R r);
[DllImport("user32.dll")]public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr c);
[DllImport("winmm.dll")]public static extern uint timeBeginPeriod(uint p);
public struct R{public int L,T,Ri,B;}}
"@
[W]::timeBeginPeriod(1)|Out-Null; [W]::SetThreadDpiAwarenessContext([IntPtr]-4)|Out-Null
$ue='C:\Program Files\Epic Games\UE_5.7\Engine\Binaries\Win64\UnrealEditor.exe'
if($Launch){$p=Start-Process $ue -ArgumentList '"C:\Users\Revaptor_FX\BongLodge_testproj\BLTest.uproject" -AutoDeclinePackageRecovery' -PassThru; $id=$p.Id}else{$id=(Get-Process UnrealEditor|?{$_.MainWindowTitle -like 'BLTest*'}|select -f 1).Id}
$sw=[Diagnostics.Stopwatch]::StartNew(); $last=@{}; $lines=New-Object Collections.Generic.List[string]
while($sw.Elapsed.TotalSeconds -lt $Seconds){
 $cb=[W+EP]{param($h,$l) $pp=0;[W]::GetWindowThreadProcessId($h,[ref]$pp)|Out-Null
  if($pp -eq $id -and [W]::IsWindowVisible($h)){$r=New-Object W+R;[W]::GetWindowRect($h,[ref]$r)|Out-Null;$sb=New-Object Text.StringBuilder 256;[W]::GetWindowText($h,$sb,256)|Out-Null
   $s="{0},{1} {2}x{3} max={4} min={5} '{6}'" -f $r.L,$r.T,($r.Ri-$r.L),($r.B-$r.T),[int][W]::IsZoomed($h),[int][W]::IsIconic($h),$sb
   $k=[string]$h; if($last[$k] -ne $s){$last[$k]=$s;$lines.Add(("{0,7} {1} {2}" -f $sw.ElapsedMilliseconds,$k,$s))}}; $true}
 [W]::EnumWindows($cb,[IntPtr]::Zero)|Out-Null; Start-Sleep -Milliseconds 15 }
$lines|Set-Content $Out; "PID=$id"; $lines

