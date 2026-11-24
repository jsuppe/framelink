# Diagnostic: which driver call keeps the 36 KB blocks when the source uploads
# through a mapped staging texture (--upload staging). NV12 4K30 600 s at
# 648fe9de; from ~100 s into the run, 260 s of ETW heap tracing (source process,
# alloc stacks) plus kernel VirtualAlloc/VirtualFree with stacks. As m6k-heap.ps1.
$ErrorActionPreference = 'Continue'
$m = New-Object System.Threading.Mutex($false, 'Local\FrameLinkFocusedWindowsValidation')
try { $got = $m.WaitOne([TimeSpan]::FromMinutes(5)) } catch [System.Threading.AbandonedMutexException] { $got = $true }
if (-not $got) { Write-Host 'mutex busy'; exit 3 }
try {
$root = '<win-home>\dev\apichk'; $d = "$root\rate0e8"; $out = "$root\m6m-heap"
if (Test-Path $out) { Write-Host "refusing: $out exists"; exit 1 }
if ((git -C $d rev-parse HEAD) -notlike '648fe9de*') { Write-Host 'wrong revision'; exit 1 }
New-Item -ItemType Directory $out | Out-Null
$xp = 'C:\Program Files (x86)\Windows Kits\10\Windows Performance Toolkit\xperf.exe'
$env:PATH = "C:\Program Files\gstreamer\1.0\msvc_x86_64\bin;$d\build;$env:PATH"
$t0 = Get-Date
$b = Start-Process -PassThru -NoNewWindow "$d\build\fl_gst_local_native_bench_win.exe" -ArgumentList "`"$d\build\gstframelink.dll`" --out `"$out\run`" --seconds 600 --width 3840 --height 2160 --fps 30 --format NV12 --upload staging"
$src = $null; while (-not $src -and ((Get-Date) - $t0).TotalSeconds -lt 30) { Start-Sleep -Milliseconds 200; $src = Get-CimInstance Win32_Process -Filter "Name='fl_gst_local_native_bench_win.exe'" | Where-Object { $_.CommandLine -match '--role source' } }
Write-Host "source pid $($src.ProcessId) at $([int]((Get-Date)-$t0).TotalSeconds) s"
while (((Get-Date) - $t0).TotalSeconds -lt 100) { Start-Sleep 1 }
& $xp -on PROC_THREAD+LOADER+VIRT_ALLOC -stackwalk VirtualAlloc+VirtualFree -BufferSize 1024 -MinBuffers 64 -MaxBuffers 1024 -f "$out\kernel.etl" -start FLHeap -heap -Pids $src.ProcessId -stackwalk HeapAlloc+HeapRealloc -BufferSize 1024 -MinBuffers 64 -MaxBuffers 1024 -f "$out\heap.etl" 2>&1 | ForEach-Object { Write-Host "xperf on: $_" }
Write-Host "trace on at $([int]((Get-Date)-$t0).TotalSeconds) s, $((Get-Date).ToString('HH:mm:ss.fff'))"
Start-Sleep 260
& $xp -stop FLHeap -stop -d "$out\merged.etl" 2>&1 | Select-Object -Last 3 | ForEach-Object { Write-Host "xperf stop: $_" }
Write-Host "trace off at $([int]((Get-Date)-$t0).TotalSeconds) s, $((Get-Date).ToString('HH:mm:ss.fff'))"
$b.WaitForExit(); Write-Host "bench exit $($b.ExitCode)"
$env:_NT_SYMBOL_PATH = "srv*$root\symcache*https://msdl.microsoft.com/download/symbols"
& $xp -i "$out\merged.etl" -o "$out\dump.txt" -symbols -a dumper 2>&1 | Select-Object -Last 2 | ForEach-Object { Write-Host "dump: $_" }
Get-ChildItem $out | ForEach-Object { '{0,14:N0} {1}' -f $_.Length, $_.Name }
tar -C $out -czf "$out\dump.tgz" dump.txt run\source.resources.csv run\config.json run\source.video.csv
Write-Host done
} finally { $m.ReleaseMutex() }
