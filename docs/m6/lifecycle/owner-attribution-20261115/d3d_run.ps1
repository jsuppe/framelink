$ErrorActionPreference='Stop'
$d='<win-home>\dev\framelink-owner-probe-20261115'; Set-Location $d
Move-Item -Force "$env:USERPROFILE\d3d_probe.cpp" "$d\d3d_probe.cpp"
$vc='C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat'
cmd /c "`"$vc`" >nul && cl /nologo /O2 /EHsc /MD d3d_probe.cpp /link d3d11.lib dxgi.lib psapi.lib" *> d3d_build.log
if($LASTEXITCODE){Get-Content d3d_build.log; throw 'build failed'}
$mutex=New-Object System.Threading.Mutex($false,'Local\FrameLinkFocusedWindowsValidation')
if(!$mutex.WaitOne(0)){throw 'runtime owner busy'}
try{ foreach($v in $args){ & .\d3d_probe.exe $v 60 "d3d-$v.csv"; "$v exit=$LASTEXITCODE" } } finally {$mutex.ReleaseMutex();$mutex.Dispose()}
