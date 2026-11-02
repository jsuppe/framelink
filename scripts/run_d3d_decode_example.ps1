param(
  [string]$BuildDir = '.\build\Release',
  [ValidateSet('gpu-color','cpu-readable')][string]$Mode = 'gpu-color',
  [switch]$VerifyPixels,
  [string]$LogDir = '.\decode-example-logs'
)
# Run in the logged-in desktop, not SSH/session0. Only this run's children are stopped.
$ErrorActionPreference = 'Stop'
$BuildDir = (Resolve-Path $BuildDir).Path
New-Item -ItemType Directory -Force $LogDir | Out-Null
$LogDir = (Resolve-Path $LogDir).Path
$channel = 'framelink.decode.' + [Guid]::NewGuid().ToString('N')
$consumer = $null; $producer = $null
try {
  $consumerArgs = $channel
  if ($VerifyPixels) { $consumerArgs += ' --verify' }
  $consumer = Start-Process -PassThru -FilePath "$BuildDir\fl_d3d_decode_consumer.exe" -ArgumentList $consumerArgs -RedirectStandardOutput "$LogDir\consumer.log" -RedirectStandardError "$LogDir\consumer.err"
  $consumerHandle = $consumer.Handle # pin process handle so ExitCode remains available
  Start-Sleep -Milliseconds 300
  $producerArgs = '"' + "$BuildDir\gstframelink.dll" + '" ' + $channel + ' ' + $Mode
  $producer = Start-Process -PassThru -FilePath "$BuildDir\fl_d3d_decode_producer.exe" -ArgumentList $producerArgs -RedirectStandardOutput "$LogDir\producer.log" -RedirectStandardError "$LogDir\producer.err"
  $producerHandle = $producer.Handle
  if (!$producer.WaitForExit(20000)) { throw 'Producer deadline exceeded' }
  if (!$consumer.WaitForExit(20000)) { throw 'Consumer deadline exceeded' }
  if ($null -eq $producer.ExitCode -or $null -eq $consumer.ExitCode) { throw 'Missing process exit status' }
  if ($producer.ExitCode -ne 0 -or $consumer.ExitCode -ne 0) { throw "Producer=$($producer.ExitCode), consumer=$($consumer.ExitCode); inspect logs" }
  $summary = Get-Content "$LogDir\consumer.log" -Raw
  if ($summary -notmatch 'received=60 gpu_completed=60 accepted_presents=60 sequence_gaps=0') { throw 'Missing complete consumer frame accounting' }
  $result = [ordered]@{ mode=$Mode; verifyPixels=[bool]$VerifyPixels; producerExit=$producer.ExitCode; consumerExit=$consumer.ExitCode; frames=60; pool=2; physicalScanout='unmeasured' }
  $result | ConvertTo-Json | Set-Content "$LogDir\result.json"
  Get-Content "$LogDir\producer.log"
  Write-Output $summary
} finally {
  foreach ($process in @($producer,$consumer)) {
    if ($null -ne $process) {
      if (!$process.HasExited) { Stop-Process -Id $process.Id }
      $process.Dispose()
    }
  }
}
