from pathlib import Path
import subprocess,base64,json,hashlib
r=Path(__file__).parent;remote='<win-home>/dev/m6-renderer9f74b9d-framelinka50841a-20261112'
s="Set-Location '"+remote+"'; $deps=@(); Get-ChildItem build/_deps -Directory -Filter '*-src' | ForEach-Object {$h=git -C $_.FullName rev-parse HEAD 2>$null; if($LASTEXITCODE -eq 0){$deps+=@{name=$_.Name;revision=$h;dirty=@(git -C $_.FullName status --porcelain)}}};$deps | ConvertTo-Json -Depth 5 | Set-Content results/dependency-revisions.json; Get-CimInstance Win32_OperatingSystem | Select-Object Caption,Version,BuildNumber | ConvertTo-Json | Set-Content results/os.json; Get-CimInstance Win32_VideoController | Select-Object Name,DriverVersion | ConvertTo-Json | Set-Content results/gpu.json; $root=(Get-Item results).FullName; Get-ChildItem results -Recurse -File | ForEach-Object {@{path=$_.FullName.Substring($root.Length+1).Replace('\\','/');sha256=(Get-FileHash $_.FullName).Hash.ToLower()}} | ConvertTo-Json"
p=subprocess.run(['ssh','-o','BatchMode=yes','<win-user>@<win-host>','powershell','-NoProfile','-EncodedCommand',base64.b64encode(s.encode('utf-16le')).decode()],capture_output=True,timeout=60,check=True);manifest=json.loads(p.stdout);(r/'remote-results-sha256.json').write_bytes(p.stdout)
for n in ['dependency-revisions.json','os.json','gpu.json']:subprocess.run(['scp','-q','<win-user>@<win-host>:'+remote+'/results/'+n,str(r/'data')],check=True)
for row in manifest:assert hashlib.sha256((r/'data'/row['path']).read_bytes()).hexdigest()==row['sha256'],row['path']
assert {row['path'] for row in manifest}=={str(p.relative_to(r/'data')) for p in (r/'data').rglob('*') if p.is_file()}
print('Verified',len(manifest),'remote results, complete local coverage')
