$ErrorActionPreference='Stop'
$m=[System.Threading.Mutex]::new($false,'Local\FrameLinkFocusedWindowsValidation')
if(-not $m.WaitOne(0)){throw 'runtime lane held'}
try {
Set-Location '<win-home>/dev/device-allocation-isolation-20261112'
$env:MSBUILDDISABLENODEREUSE='1'
cmake -S . -B build -A x64
if($LASTEXITCODE){throw 'configure'}
cmake --build build --config Release -- /nr:false
if($LASTEXITCODE){throw 'build'}
Get-FileHash build/Release/device_allocation_isolation.exe | ConvertTo-Json | Set-Content before.json
foreach($a in @(@('hardware','empty'),@('hardware','textures'),@('warp','empty'),@('warp','textures'))){
 $name=$a[0]+'-'+$a[1]
 & ./build/Release/device_allocation_isolation.exe $a[0] $a[1] > ($name+'.csv') 2> ($name+'.stderr')
 if($LASTEXITCODE){throw ('probe failed '+$name)}
}
Get-FileHash build/Release/device_allocation_isolation.exe | ConvertTo-Json | Set-Content after.json
@{state='finished';utc=[DateTime]::UtcNow.ToString('o')} | ConvertTo-Json | Set-Content status.json
} finally {$m.ReleaseMutex();$m.Dispose()}
