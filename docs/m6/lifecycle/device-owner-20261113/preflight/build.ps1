$ErrorActionPreference='Stop'
$mutex=New-Object System.Threading.Mutex($false,'Local\FrameLinkFocusedWindowsValidation')
if(!$mutex.WaitOne(0)){throw 'runtime owner busy'}
try {
 Set-Location '<win-home>/dev/framelink-device-owner-20261113'
 & tar -xf source.tar -C source
 if($LASTEXITCODE){throw 'extract failed'}
 & cmake -S source -B build -G 'Visual Studio 17 2022' -A x64 -DFRAMELINK_BUILD_TESTS=ON -DFRAMELINK_BUILD_GST=ON -DFRAMELINK_BUILD_SAMPLES=ON *> configure.log
 if($LASTEXITCODE){throw 'configure failed'}
 & cmake --build build --config Release --target fl_device_owner_win framelink_gst_session gstframelink fl_gst_av_bench --parallel 4 *> build.log
 $code=$LASTEXITCODE
 @{utc=[DateTime]::UtcNow.ToString('o');build_exit=$code} | ConvertTo-Json | Set-Content status.json
 exit $code
} finally {$mutex.ReleaseMutex();$mutex.Dispose()}
