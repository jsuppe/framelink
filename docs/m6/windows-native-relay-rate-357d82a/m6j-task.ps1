# Runs in the interactive session (scheduled task /IT), as a presenting app
# would: wakes the display (zero-delta mouse move, as vkrender does at
# presentation start), holds system and display awake for the whole session
# (so no idle timeout can fall in the gaps between captures), waits 30 s, then
# runs the formal Windows 4K30 rate cells at 357d82a5. The bench relay also
# holds the display during each capture.
$root = '<win-home>\dev\apichk'; $d = "$root\rate0e8"; $out = "$root\out-357d82a-4k30-lit"; $log = "$root\m6j-task.log"
function L($s) { Add-Content $log ("{0} {1}" -f (Get-Date).ToString('HH:mm:ss.fff'), $s) }
Add-Type -Namespace W -Name U -MemberDefinition @'
[DllImport("user32.dll")] public static extern void mouse_event(uint f, int dx, int dy, uint d, System.UIntPtr e);
[DllImport("kernel32.dll")] public static extern uint SetThreadExecutionState(uint f);
'@
[W.U]::mouse_event(1, 0, 0, 0, [UIntPtr]::Zero); L 'display wake (zero-delta mouse move)'
$prev = [W.U]::SetThreadExecutionState(0x80000003); L "session hold (ES_CONTINUOUS|ES_SYSTEM_REQUIRED|ES_DISPLAY_REQUIRED) returned $prev"
Start-Sleep 30
$env:PATH = "C:\Program Files\gstreamer\1.0\msvc_x86_64\bin;$d\build;$env:PATH"; Set-Location $d
$py = (Get-Command python).Source
L 'cells start'
& $py tools\run_local_native_rate_cells.py --exe "$d\build\fl_gst_local_native_bench_win.exe" --plugin "$d\build\gstframelink.dll" --out $out --cells bgra4k30 nv12-4k30 --duration 1800 2>&1 | ForEach-Object { L "cells: $_" }
L "cells exit $LASTEXITCODE"
[W.U]::SetThreadExecutionState(0x80000000) | Out-Null; L 'session hold released'
L 'done'
