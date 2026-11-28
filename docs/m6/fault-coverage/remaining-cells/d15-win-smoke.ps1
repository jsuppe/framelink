# Decision 15 (C2): build a revision from a git bundle and smoke the Windows
# fault driver on one cell, under the validation mutex. The full Windows
# campaign runs once, inside decision 14's requalification.
param([string]$rev, [string]$cell = 'windows-native-nv12-1080p60', [int]$repeats = 1)
$ErrorActionPreference = 'Continue'
$m = New-Object System.Threading.Mutex($false, 'Local\FrameLinkFocusedWindowsValidation')
try { $got = $m.WaitOne([TimeSpan]::FromHours(2)) } catch [System.Threading.AbandonedMutexException] { $got = $true }
if (-not $got) { Write-Host 'mutex busy'; exit 3 }
try {
$root = "$env:USERPROFILE\dev\apichk\d15"; $d = "$root\$rev"
New-Item -ItemType Directory -Force -Path $root | Out-Null
$vs = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -property installationPath
$env:GSTREAMER_1_0_ROOT_MSVC_X86_64 = 'C:\Program Files\gstreamer\1.0\msvc_x86_64\'
$env:PATH = "$env:GSTREAMER_1_0_ROOT_MSVC_X86_64" + "bin;$env:PATH"
if (-not (Test-Path $d)) {
  git clone -q --no-checkout "$env:USERPROFILE\d15.bundle" $d; git -C $d checkout -q --detach $rev
  cmd /c "call `"$vs\VC\Auxiliary\Build\vcvars64.bat`" >nul && cd /d $d && cmake --preset ci >nul && cmake --build --preset ci 2>&1" | Select-String -Pattern 'error C|LNK[0-9]|FAILED' | Select-Object -First 10 | ForEach-Object { Write-Host "build: $_" }
  Write-Host "build exit $LASTEXITCODE"
}
Write-Host ("revision " + (git -C $d rev-parse HEAD))
Get-FileHash "$d\build\fl_gst_av_bench.exe", "$d\build\gstframelink.dll" | ForEach-Object { Write-Host ("sha256 " + $_.Hash.Substring(0, 16).ToLower() + " " + (Split-Path $_.Path -Leaf)) }
$out = "$root\out-$rev\$cell"
& "$d\docs\m6\fault-coverage\remaining-cells\run_cells.ps1" "$d\build\fl_gst_av_bench.exe" "$d\build\gstframelink.dll" $out $cell $repeats
Write-Host done
} finally { $m.ReleaseMutex() }
