# Interactive-session helper started during the lit-display run (the task's own
# session hold failed: 0x80000003 parsed as a negative Int32). Holds system and
# display awake until the run's task log says done, at most 90 min.
$log = '<win-home>\dev\apichk\m6j-hold.log'; $tlog = '<win-home>\dev\apichk\m6j-task.log'
function L($s) { Add-Content $log ("{0} {1}" -f (Get-Date).ToString('HH:mm:ss.fff'), $s) }
Add-Type -Namespace W -Name K -MemberDefinition '[DllImport("kernel32.dll")] public static extern uint SetThreadExecutionState(uint f);'
$want = [Convert]::ToUInt32('80000003', 16)
$prev = [W.K]::SetThreadExecutionState($want); L ("hold set, previous state 0x{0:X8}" -f $prev)
$end = (Get-Date).AddMinutes(90)
while ((Get-Date) -lt $end -and -not (Select-String -Quiet -Path $tlog -Pattern ' done$')) { Start-Sleep 5 }
[W.K]::SetThreadExecutionState([Convert]::ToUInt32('80000000', 16)) | Out-Null; L 'hold released'
