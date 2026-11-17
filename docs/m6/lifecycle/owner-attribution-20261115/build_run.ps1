$ErrorActionPreference='Stop'
$d='<win-home>\dev\framelink-owner-probe-20261115'; $b='<win-home>\dev\framelink-device-owner-20261113'
New-Item -ItemType Directory $d -Force | Out-Null; Set-Location $d
Move-Item -Force "$env:USERPROFILE\owner_probe.cpp" "$d\owner_probe.cpp"
Copy-Item "$b\build\Release\framelink.dll" $d -Force
$vc='C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat'
cmd /c "`"$vc`" >nul && cl /nologo /O2 /EHsc /MD /I`"$b\source\include`" owner_probe.cpp /link `"$b\build\Release\framelink.lib`" psapi.lib" *> build.log
if($LASTEXITCODE){Get-Content build.log; throw 'build failed'}
(Get-FileHash framelink.dll -Algorithm SHA256).Hash + '  framelink.dll' | Set-Content sha256.txt
(Get-FileHash owner_probe.exe -Algorithm SHA256).Hash + '  owner_probe.exe' | Add-Content sha256.txt
$mutex=New-Object System.Threading.Mutex($false,'Local\FrameLinkFocusedWindowsValidation')
if(!$mutex.WaitOne(0)){throw 'runtime owner busy'}
try{
 foreach($arm in $args){ $t=Get-Date; & .\owner_probe.exe parent $arm 60 "$arm.csv"; "$arm exit=$LASTEXITCODE secs=$([int]((Get-Date)-$t).TotalSeconds)" | Tee-Object -Append status.txt }
} finally {$mutex.ReleaseMutex();$mutex.Dispose()}
