# Re-dump the three noisy-host stall windows with Microsoft public symbols.
$ErrorActionPreference = 'Continue'
$d = '<win-home>\dev\apichk\stallprobe\noisy'; $cache = '<win-home>\dev\apichk\symcache'
New-Item -ItemType Directory -Force $cache | Out-Null
$env:_NT_SYMBOL_PATH = "srv*$cache*https://msdl.microsoft.com/download/symbols"
$x = (Get-Command xperf -ErrorAction SilentlyContinue).Source
if (-not $x) { $x = "${env:ProgramFiles(x86)}\Windows Kits\10\Windows Performance Toolkit\xperf.exe" }
foreach ($w in @(@('b1',243518671),@('b2',264710345),@('b3',415216767))) {
 $t=$w[1]; $o="$d\sym-$($w[0]).txt"
 $began=Get-Date
 & $x -i "$d\run.etl" -o $o -symbols -a dumper -range ($t-150000) ($t+300000) -stacktimeshifting 2>&1 | Select-Object -Last 3 | ForEach-Object { Write-Host "xperf: $_" }
 Write-Host ("$($w[0]) " + (Get-Item $o).Length + " bytes, " + [int]((Get-Date)-$began).TotalSeconds + " s")
}
Write-Host ("symcache " + [int]((Get-ChildItem $cache -Recurse | Measure-Object Length -Sum).Sum/1MB) + " MB")
Write-Host done
