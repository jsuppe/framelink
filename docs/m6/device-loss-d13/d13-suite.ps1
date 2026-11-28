# Decision 13: the whole Windows suite on an already-built revision directory.
param([string]$dir)
$ErrorActionPreference = 'Continue'
$m = New-Object System.Threading.Mutex($false, 'Local\FrameLinkFocusedWindowsValidation')
try { $got = $m.WaitOne([TimeSpan]::FromHours(1)) } catch [System.Threading.AbandonedMutexException] { $got = $true }
if (-not $got) { Write-Host 'mutex busy'; exit 3 }
try {
$d = "$env:USERPROFILE\dev\apichk\d13\$dir"
$vs = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -property installationPath
$env:GSTREAMER_1_0_ROOT_MSVC_X86_64 = 'C:\Program Files\gstreamer\1.0\msvc_x86_64\'
$env:PATH = "$env:GSTREAMER_1_0_ROOT_MSVC_X86_64" + "bin;$env:PATH"
Write-Host ("revision " + (git -C $d rev-parse HEAD))
cmd /c "call `"$vs\VC\Auxiliary\Build\vcvars64.bat`" >nul && cd /d $d && ctest --test-dir build 2>&1" | Select-String -Pattern 'tests passed|\*\*\*' | ForEach-Object { Write-Host "suite: $_" }
Write-Host done
} finally { $m.ReleaseMutex() }
