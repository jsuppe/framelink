$ErrorActionPreference='Stop'
$ProgressPreference='SilentlyContinue'
function Assert-Idle {
 $active=@(Get-CimInstance Win32_Process | Where-Object {
  $_.Name -match '^(fl_.*|framelink.*|cmake|ctest|MSBuild)\.exe$' -or
  ($_.Name -match '^python(w|3)?\.exe$' -and $_.CommandLine -match 'm5_external_pipeline')
 })
 if($active.Count){$active | Select-Object ProcessId,Name,CommandLine | Format-List;throw 'Runtime busy'}
}
Set-Location '<win-home>/dev/framelink-startup-phase-461584b-20261112T062145Z-e9d244c0'
Assert-Idle
if((Test-Path results/audio) -or (Test-Path results/audio-coordinated)){throw 'Media already attempted'}
foreach($row in (Get-Content results/binaries.json -Raw | ConvertFrom-Json)){
 if((Get-FileHash $row.Path -Algorithm SHA256).Hash -ne $row.Hash){throw 'Binary changed'}
}
$mutex=New-Object System.Threading.Mutex($false,'Local\FrameLinkFocusedWindowsValidation')
$locked=$mutex.WaitOne(0);if(!$locked){throw 'Lane held'}
Copy-Item results/status.json results/status-pre-resume.json
$state=@{revision='461584bbf5a6b8b70cec1578d69d35cb4b1ea2ff';status='resuming-verified-binaries';scope='diagnostic, not acceptance'}
function Save-State {$state | ConvertTo-Json -Depth 5 | Set-Content results/status.json}
$env:PATH='C:/Program Files/gstreamer/1.0/msvc_x86_64/bin;'+$env:PATH
try {
 Get-ChildItem Env: | Where-Object {$_.Name -like 'FRAMELINK_TEST*' -or $_.Name -like 'FRAMELINK_TRACE*' -or $_.Name -like 'FRAMELINK_GST_TRACE*' -or $_.Name -eq 'FRAMELINK_SETUP_PHASE_DIR'} | ForEach-Object {Remove-Item ('Env:'+ $_.Name)}
 $env:PATH=(Resolve-Path build/Release).Path+';'+$env:PATH
 $env:GST_DEBUG='framelink:4'
 foreach($mode in @('audio','audio-coordinated')){
  Assert-Idle
  $out=(New-Item -ItemType Directory ('results/'+$mode)).FullName
  $env:FRAMELINK_SETUP_PHASE_DIR=$out
  $env:FRAMELINK_TRACE=$out+'/native.csv'
  $env:FRAMELINK_GST_TRACE=$out+'/gst.csv'
  $state.status='running-'+$mode;Save-State
  # Recorder adds .PID.csv; each arm has its own directory. No overwritten shared trace.
  & python source/test/m5_external_pipeline.py --build build/Release --output $out --audio-sink fakesink --clock-policy $mode *> ('results/'+$mode+'-command.log')
  $state[$mode+'Exit']=$LASTEXITCODE;Save-State
  # A failure is evidence, not a retry request. Continue only after peers exited.
  Assert-Idle
 }

 $state.status='finished'
} catch {$state.status='failed';$state.error=$_.Exception.Message}
finally {$state.finishedUtc=[DateTime]::UtcNow.ToString('o');Save-State;if($locked){$mutex.ReleaseMutex()};$mutex.Dispose()}
if($state.status -ne 'finished'){exit 1}
exit 0
