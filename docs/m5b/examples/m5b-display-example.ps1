$ErrorActionPreference='Continue'
$root='<win-tree>\build\Release'
$out='<win-home>\dev\m5b-display-example'
Remove-Item -Recurse -Force $out -ErrorAction SilentlyContinue; New-Item -ItemType Directory $out | Out-Null
$env:PATH="$root;C:\Program Files\gstreamer\1.0\msvc_x86_64\bin;"+$env:PATH
$env:GST_PLUGIN_PATH=$root
$exe="$root\framelink_gst_av.exe"
$session=Join-Path $out 'session'
$c=Start-Process $exe -ArgumentList @('consumer','--session',$session,'--display','--size','640x360','--seconds','5','--timeout-ms','10000') -PassThru -WindowStyle Normal -RedirectStandardError "$out\consumer.err"; $null=$c.Handle
Start-Sleep -Milliseconds 300
$p=Start-Process $exe -ArgumentList @('producer','--session',$session,'--size','640x360','--seconds','5','--timeout-ms','10000','--video-source','"videotestsrc is-live=true pattern=ball"') -PassThru -WindowStyle Hidden -RedirectStandardError "$out\producer.err"; $null=$p.Handle
$p.WaitForExit(30000)|Out-Null;$c.WaitForExit(30000)|Out-Null
"producer=$($p.ExitCode) consumer=$($c.ExitCode)" | Set-Content "$out\result.txt"
