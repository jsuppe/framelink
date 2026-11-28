# M6.4 decision 15 on Windows: the same declared fault case set as
# run_cells.sh, one cell, primary zero-delay profile, REPEATS each.
#   run_cells.ps1 BENCH PLUGIN OUT_DIR CELL [REPEATS]
#   CELL: windows-MEMORY-FORMAT-SIZE[-element], MEMORY native or shared-cpu,
#         e.g. windows-native-nv12-1080p60, windows-shared-cpu-bgra-4k30
# Declared outcomes as in run_cells.sh: native receivers hold the slot (200 ms
# stall: counted drop, explicit failure under error-on-ring-full; 200 ms burst
# absorbed); shared CPU delivers late (200 ms stall and burst absorbed).
param([string]$bench, [string]$plugin, [string]$out, [string]$cell, [int]$repeats = 5)
$ErrorActionPreference = 'Stop'
New-Item -ItemType Directory -Force -Path $out | Out-Null
$parts = $cell -split '-'
if ($parts[0] -ne 'windows') { throw "unknown platform in $cell" }
if ($parts[1] -eq 'shared') { $memory = 'shared-cpu'; $rest = $parts[3..($parts.Count - 1)] } else { $memory = $parts[1]; $rest = $parts[2..($parts.Count - 1)] }
$fmt = $rest[0].ToUpper(); $size = $rest[1]; $form = if ($rest.Count -gt 2) { $rest[2] } else { '' }
switch ($size) {
  '1080p60' { $geom = @('--size', '1920x1080', '--fps', '60', '--arrival-budget-ms', '17'); $vframe = '360' }
  '4k30' { $geom = @('--size', '3840x2160', '--fps', '30', '--arrival-budget-ms', '34'); $vframe = '180' }
  default { throw "unknown size $size" }
}
switch ($memory) {
  'native' { $mem = @('--native', '--format', $fmt); $stall200 = 'counted-drop'; $burst200 = 'absorbed'; $strict = $true }
  'shared-cpu' { $mem = @('--shared-cpu', '--format', $fmt); $stall200 = 'absorbed'; $burst200 = 'absorbed'; $strict = $false }
  default { throw "unknown memory $memory" }
}
if ($form -eq 'element') { $mem += @('--mode', 'element') } elseif ($form) { throw "unknown form $form" }
if ($env:STALL200) { $stall200 = $env:STALL200 }
if ($env:BURST200) { $burst200 = $env:BURST200 }
$profile = $geom + @('--pool', '2', '--video-queue', '1', '--output-queue-buffers', '1', '--latency-ms', '0', '--transport-only', '--no-output-preroll', '--seconds', '12', '--raw')
function Run([string]$label, [string[]]$extra) {
  $argv = @($plugin) + $profile + $mem + $extra + @('--out', $out, '--label', $label)
  $p = Start-Process -FilePath $bench -ArgumentList $argv -NoNewWindow -Wait -PassThru -RedirectStandardOutput "$out\$label.stdout" -RedirectStandardError "$out\$label.stderr"
  $pass = ''
  if (Test-Path "$out\$label.summary.txt") { $pass = (Select-String -Path "$out\$label.summary.txt" -Pattern '^fault.pass=' | Select-Object -First 1).Line }
  $line = "$label rc=$($p.ExitCode) $pass"
  Add-Content -Path "$out\run.log" -Value $line; Write-Host $line
}
foreach ($i in 1..$repeats) {
  Run "$cell-producer-kill-$i" @('--fault', 'producer-kill', '--fault-at-s', '6')
  Run "$cell-consumer-kill-$i" @('--fault', 'consumer-kill', '--fault-at-s', '6')
  Run "$cell-stall5-$i" @('--fault', 'consumer-stall', '--fault-frame', $vframe, '--fault-ms', '5', '--fault-expect', 'absorbed')
  Run "$cell-stall200-$i" @('--fault', 'consumer-stall', '--fault-frame', $vframe, '--fault-ms', '200', '--fault-expect', $stall200)
  if ($strict) { Run "$cell-stall200-strict-$i" @('--fault', 'consumer-stall', '--fault-frame', $vframe, '--fault-ms', '200', '--fault-expect', 'explicit-failure', '--error-on-ring-full') }
  Run "$cell-burst5-$i" @('--burst-queue', '--fault', 'producer-burst', '--fault-frame', $vframe, '--fault-ms', '5', '--fault-expect', 'absorbed')
  Run "$cell-burst200-$i" @('--burst-queue', '--fault', 'producer-burst', '--fault-frame', $vframe, '--fault-ms', '200', '--fault-expect', $burst200)
  Run "$cell-astall50-$i" @('--burst-queue', '--fault', 'audio-stall', '--fault-frame', '600', '--fault-ms', '50', '--fault-expect', 'absorbed')
  Run "$cell-astall200-$i" @('--burst-queue', '--fault', 'audio-stall', '--fault-frame', '600', '--fault-ms', '200', '--fault-expect', 'absorbed')
  Run "$cell-adrop50-$i" @('--fault', 'audio-drop', '--fault-frame', '600', '--fault-ms', '50', '--fault-expect', 'explicit-failure')
  Run "$cell-agap50-$i" @('--fault', 'audio-gap', '--fault-frame', '600', '--fault-ms', '50', '--fault-expect', 'gap-visible')
  Run "$cell-video-no-pts-$i" @('--fault', 'video-no-pts', '--fault-frame', $vframe, '--fault-expect', 'explicit-failure')
  Run "$cell-video-stale-meta-$i" @('--fault', 'video-stale-meta', '--fault-frame', $vframe, '--fault-expect', 'explicit-failure')
  Run "$cell-audio-no-pts-$i" @('--fault', 'audio-no-pts', '--fault-frame', '600', '--fault-expect', 'explicit-failure')
  Run "$cell-audio-misaligned-$i" @('--fault', 'audio-misaligned', '--fault-frame', '600', '--fault-expect', 'explicit-failure')
}
