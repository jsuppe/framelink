# Diagnostic repeats of the two suite failures at c485ea44 (the failed suite run stands).
$ErrorActionPreference = 'Continue'
$m = New-Object System.Threading.Mutex($false, 'Local\FrameLinkFocusedWindowsValidation')
try { $got = $m.WaitOne([TimeSpan]::FromMinutes(5)) } catch [System.Threading.AbandonedMutexException] { $got = $true }
if (-not $got) { Write-Host 'mutex busy'; exit 3 }
try {
$root = '%USERPROFILE%\dev\apichk'; $d = "$root\suite-c485ea4"
$vs = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -property installationPath
$env:GSTREAMER_1_0_ROOT_MSVC_X86_64 = 'C:\Program Files\gstreamer\1.0\msvc_x86_64\'
$env:PATH = "$env:GSTREAMER_1_0_ROOT_MSVC_X86_64" + "bin;$d\build;$env:PATH"
Set-Location $d
Get-Process | Where-Object { $_.Name -match '^fl_|ctest|vkrender' } | ForEach-Object { Write-Host "running: $($_.Name) $($_.Id)" }
cmd /c "call `"$vs\VC\Auxiliary\Build\vcvars64.bat`" >nul && cd /d $d && ctest --test-dir build -R ^read_lease_d3d11_producer_death$ --repeat until-fail:300 --output-on-failure > $root\rep2-d3d11-death.log 2>&1"
Write-Host "d3d11 death repeats exit $LASTEXITCODE"; Get-Content "$root\rep2-d3d11-death.log" | Select-String 'Failed|line |tests passed|Test #' | Select-Object -Last 6 | ForEach-Object { Write-Host "$_" }
cmd /c "call `"$vs\VC\Auxiliary\Build\vcvars64.bat`" >nul && cd /d $d && ctest --test-dir build -R ^gst_native_producer_win$ --repeat until-fail:100 --output-on-failure > $root\rep2-native-producer.log 2>&1"
Write-Host "native producer repeats exit $LASTEXITCODE"; Get-Content "$root\rep2-native-producer.log" | Select-String 'Timeout|Failed|tests passed|Test command' | Select-Object -Last 6 | ForEach-Object { Write-Host "$_" }
} finally { $m.ReleaseMutex() }
