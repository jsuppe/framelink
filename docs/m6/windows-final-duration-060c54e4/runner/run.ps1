$ErrorActionPreference='Stop'
$d='<win-home>\dev\framelink-final-duration-060c54e4'
if(Test-Path $d){throw 'exists'}; New-Item -ItemType Directory $d | Out-Null
foreach($f in 'remote.py','m6_raw_timing.py','analyze_local_4k30.py','analyze_local_1080p60.py','check_local_native_bench.py','dll-pins.json'){ Move-Item "$env:USERPROFILE\dur-upload\$f" "$d\$f" }
$b='<win-home>\dev\framelink-final-final-060c54e4\build\Release'
foreach($n in 'fl_gst_av_bench.exe','fl_gst_local_native_bench_win.exe','gstframelink.dll','framelink_gst_session.dll','framelink.dll'){ if(!(Test-Path "$b\$n")){throw "missing $n"} }
$mutex=New-Object System.Threading.Mutex($false,'Local\FrameLinkFocusedWindowsValidation')
if(!$mutex.WaitOne(0)){throw 'runtime owner busy'}
try{ & python -E "$d\remote.py"; "exit=$LASTEXITCODE" } finally {$mutex.ReleaseMutex();$mutex.Dispose()}
