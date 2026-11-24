# Runs in the interactive session (scheduled task /IT). Waits for the long
# capture directory named in blink.cfg, then turns the monitor off and on on a
# fixed schedule, logging each step with a timestamp.
$cfg = Get-Content '<win-home>\dev\apichk\blink.cfg'
$watch = $cfg[0]; $log = $cfg[1]
Add-Type -Namespace W -Name U -MemberDefinition @'
[DllImport("user32.dll")] public static extern bool PostMessage(System.IntPtr h, uint m, System.IntPtr w, System.IntPtr l);
[DllImport("user32.dll")] public static extern void mouse_event(uint f, int dx, int dy, uint d, System.UIntPtr e);
'@
function L($s) { Add-Content $log ("{0} {1}" -f (Get-Date).ToString('HH:mm:ss.fff'), $s) }
L "waiting for $watch"
$deadline = (Get-Date).AddMinutes(10)
while (-not (Test-Path $watch)) { if ((Get-Date) -gt $deadline) { L 'timeout waiting'; exit 1 }; Start-Sleep -Milliseconds 200 }
$t0 = Get-Date; L 'capture started'
foreach ($c in 0..3) {
  $off = 60 + 120*$c; $on = $off + 30
  while (((Get-Date) - $t0).TotalSeconds -lt $off) { Start-Sleep -Milliseconds 100 }
  [W.U]::PostMessage([IntPtr]0xffff, 0x0112, [IntPtr]0xF170, [IntPtr]2) | Out-Null; L "cycle $c monitor off"
  while (((Get-Date) - $t0).TotalSeconds -lt $on) { Start-Sleep -Milliseconds 100 }
  [W.U]::mouse_event(1, 1, 0, 0, [UIntPtr]::Zero); Start-Sleep -Milliseconds 50; [W.U]::mouse_event(1, -1, 0, 0, [UIntPtr]::Zero); L "cycle $c monitor on (input)"
}
L 'done'
