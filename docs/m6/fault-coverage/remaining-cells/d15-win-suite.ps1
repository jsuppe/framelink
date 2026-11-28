# Decision 15: the whole Windows suite on a revision built by d15-win-smoke.ps1
# (the native sink error split touches code shared with Windows).
param([string]$rev)
$ErrorActionPreference = 'Continue'
$m = New-Object System.Threading.Mutex($false, 'Local\FrameLinkFocusedWindowsValidation')
try { $got = $m.WaitOne([TimeSpan]::FromHours(2)) } catch [System.Threading.AbandonedMutexException] { $got = $true }
if (-not $got) { Write-Host 'mutex busy'; exit 3 }
try {
$d = "$env:USERPROFILE\dev\apichk\d15\$rev"
$vs = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -property installationPath
$env:GSTREAMER_1_0_ROOT_MSVC_X86_64 = 'C:\Program Files\gstreamer\1.0\msvc_x86_64\'
$env:PATH = "$env:GSTREAMER_1_0_ROOT_MSVC_X86_64" + "bin;$env:PATH"
Write-Host ("revision " + (git -C $d rev-parse HEAD))
cmd /c "call `"$vs\VC\Auxiliary\Build\vcvars64.bat`" >nul && cd /d $d && ctest --test-dir build 2>&1" | Select-String -Pattern 'tests passed|\*\*\*' | ForEach-Object { Write-Host "suite: $_" }
Write-Host done
} finally { $m.ReleaseMutex() }
