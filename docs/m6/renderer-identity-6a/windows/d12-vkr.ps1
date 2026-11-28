# Decision 12, B1: the Windows renderer identity evidence, rerun with the
# vkrender changes of PR #25 (two-frame receive queue and 13-slot ring, drain
# per iteration, frames pending at a session end presented). Same case as the
# a6a1906 proof (renderer-integration-a6a1906/probe.py): native NV12 1080p60
# A/V into vkrenderer --serve, 5 s, traces on; latency 0 and 100 ms, three
# captures each. Builds and runs under the validation mutex.
param([string]$vkrRev, [string]$flRev, [int]$reps = 3)
$ErrorActionPreference = 'Continue'
$m = New-Object System.Threading.Mutex($false, 'Local\FrameLinkFocusedWindowsValidation')
try { $got = $m.WaitOne([TimeSpan]::FromHours(8)) } catch [System.Threading.AbandonedMutexException] { $got = $true }
if (-not $got) { Write-Host 'mutex busy'; exit 3 }
try {
$root = "$env:USERPROFILE\dev\apichk\d12-vkr"
if (Test-Path $root) { Write-Host "refusing: $root exists"; exit 1 }
New-Item -ItemType Directory $root | Out-Null
function L($s) { $line = "{0} {1}" -f (Get-Date).ToUniversalTime().ToString('yyyy-MM-ddTHH:mm:ss.fffZ'), $s; Add-Content "$root\run.log" $line; Write-Host $line }
git clone -q --no-checkout "$env:USERPROFILE\d12-framelink.bundle" "$root\framelink"; git -C "$root\framelink" checkout -q --detach $flRev
git clone -q --no-checkout "$env:USERPROFILE\d12-vkrender.bundle" "$root\vkrender"; git -C "$root\vkrender" checkout -q --detach $vkrRev
L ("framelink " + (git -C "$root\framelink" rev-parse HEAD) + " vkrender " + (git -C "$root\vkrender" rev-parse HEAD))
$vs = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -property installationPath
$env:GSTREAMER_1_0_ROOT_MSVC_X86_64 = 'C:\Program Files\gstreamer\1.0\msvc_x86_64\'
$env:PATH = "$env:GSTREAMER_1_0_ROOT_MSVC_X86_64" + "bin;$env:PATH"
cmd /c "call `"$vs\VC\Auxiliary\Build\vcvars64.bat`" >nul && cd /d $root\vkrender && cmake --preset ci >nul && cmake --build --preset ci 2>&1" | Select-String -Pattern 'error C|LNK[0-9]|FAILED' | Select-Object -First 10 | ForEach-Object { L "build: $_" }
L "build exit $LASTEXITCODE"
$renderer = Get-ChildItem "$root\vkrender\build" -Recurse -Filter vkrenderer.exe | Select-Object -First 1
$producer = Get-ChildItem "$root\vkrender\build" -Recurse -Filter framelink_av_producer.exe | Select-Object -First 1
$plugin = Get-ChildItem "$root\vkrender\build" -Recurse -Filter gstframelink.dll | Select-Object -First 1
if (-not $renderer -or -not $producer -or -not $plugin) { L 'missing build outputs'; exit 1 }
if ($producer.DirectoryName -ne $plugin.DirectoryName) { Copy-Item $plugin.FullName $producer.DirectoryName }
$b = $producer.DirectoryName
foreach ($f in $renderer.FullName, $producer.FullName, "$b\gstframelink.dll", "$b\framelink.dll") { if (Test-Path $f) { L ("sha256 {0} {1}" -f (Split-Path $f -Leaf), (Get-FileHash $f).Hash) } }
$env:PATH = "$b;$($renderer.DirectoryName);$env:PATH"
$py = (Get-Command python).Source
New-Item -ItemType Directory "$root\results" | Out-Null
foreach ($lat in 0, 100) {
  foreach ($rep in 1..$reps) {
    $label = "lat$lat-$rep"
    & $py -E "$env:USERPROFILE\d12-vkr-probe.py" --build $b --renderer $renderer.FullName --output "$root\results\$label" --seconds 5 --case 1920x1080-60-NV12-av --trace on --native --latency-ms $lat *> "$root\$label-controller.log"
    L "$label exit $LASTEXITCODE"
  }
}
L 'done'
} finally { $m.ReleaseMutex() }
