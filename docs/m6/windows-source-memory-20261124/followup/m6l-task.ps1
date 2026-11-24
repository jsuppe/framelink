# Runs in the interactive session (scheduled task /IT): wakes the display,
# holds system and display awake for the whole session, waits 30 s, then at
# 648fe9de:
#  1. the formal Windows 4K30 rate cells with --upload staging (1800 s each);
#  2. a 3600 s NV12 4K30 capture with the default UpdateSubresource upload
#     (diagnostic; the checker freezes 1800 s, so it is analyzed separately).
$root = '<win-home>\dev\apichk'; $d = "$root\rate0e8"; $log = "$root\m6l-task.log"
$cells = "$root\out-648fe9d-staging"; $long = "$root\out-648fe9d-3600"
function L($s) { Add-Content $log ("{0} {1}" -f (Get-Date).ToString('HH:mm:ss.fff'), $s) }
Add-Type -Namespace W -Name U -MemberDefinition @'
[DllImport("user32.dll")] public static extern void mouse_event(uint f, int dx, int dy, uint d, System.UIntPtr e);
[DllImport("kernel32.dll")] public static extern uint SetThreadExecutionState(uint f);
'@
[W.U]::mouse_event(1, 0, 0, 0, [UIntPtr]::Zero); L 'display wake (zero-delta mouse move)'
$prev = [W.U]::SetThreadExecutionState([Convert]::ToUInt32('80000003', 16)); L ("session hold set, previous state 0x{0:X8}" -f $prev)
Start-Sleep 30
$env:PATH = "C:\Program Files\gstreamer\1.0\msvc_x86_64\bin;$d\build;$env:PATH"; Set-Location $d
$py = (Get-Command python).Source
L 'staging cells start'
& $py tools\run_local_native_rate_cells.py --exe "$d\build\fl_gst_local_native_bench_win.exe" --plugin "$d\build\gstframelink.dll" --out $cells --cells bgra4k30 nv12-4k30 --duration 1800 --upload staging 2>&1 | ForEach-Object { L "cells: $_" }
L "staging cells exit $LASTEXITCODE"
New-Item -ItemType Directory $long | Out-Null
L '3600 s default-upload capture start'
$p = Start-Process -PassThru -NoNewWindow "$d\build\fl_gst_local_native_bench_win.exe" -ArgumentList "`"$d\build\gstframelink.dll`" --out `"$long\nv12-4k30-3600s`" --seconds 3600 --width 3840 --height 2160 --fps 30 --format NV12"
$h = $p.Handle; $p.WaitForExit(); L "3600 s capture exit $($p.ExitCode)"
[W.U]::SetThreadExecutionState([Convert]::ToUInt32('80000000', 16)) | Out-Null; L 'session hold released'
L 'done'
