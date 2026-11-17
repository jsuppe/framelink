from pathlib import Path
import subprocess,base64,json,hashlib
root=Path('<linux-home>/dev/evidence/m6-cpu-path/windows-cpu-bgra1080-461584b-prepared/runs/bgra1080-461584b-20261112t1925z');d=root/'data';remote=json.loads((root/'controller-status.json').read_text())['remote']
code="Set-Location '"+remote+"'; Get-ChildItem -Recurse -File | ForEach-Object {@{path=$_.FullName.Substring((Get-Location).Path.Length+1).Replace('\\','/');sha256=(Get-FileHash $_.FullName).Hash.ToLower()}} | ConvertTo-Json"
p=subprocess.run(['ssh','-o','BatchMode=yes','<win-user>@<win-host>','powershell','-NoProfile','-EncodedCommand',base64.b64encode(code.encode('utf-16le')).decode()],capture_output=True,check=True,timeout=60);manifest=json.loads(p.stdout);(root/'remote-sha256.json').write_bytes(p.stdout)
for x in manifest:assert hashlib.sha256((d/x['path']).read_bytes()).hexdigest()==x['sha256'],x['path']
assert {x['path'] for x in manifest}=={str(x.relative_to(d)) for x in d.rglob('*') if x.is_file()}
for name,h in json.loads((d/'artifact-sha256.json').read_text()).items():assert hashlib.sha256((d/name).read_bytes()).hexdigest()==h
s=json.loads((d/'status.json').read_text());assert s['state']=='finished';pins=json.loads((d/'binary-pins.json').read_text());assert s['binary_sha256']==pins
for n,v in s.items():
 if n.startswith('windows-shared-'):assert v['exit']==0 and v['binary_sha256']==pins
idle=json.loads((d/'idle-final.json').read_text(encoding='utf-8-sig'));assert idle['count']==0
for sec in [5,1800]:
 f=d/f'windows-shared-bgra-1920x1080-60-{sec}';p=subprocess.run(['python3',str(d/'analyze_local.py'),str(f),str(sec)],capture_output=True,check=True);r=json.loads(p.stdout);(root/f'local-analysis-{sec}.json').write_bytes(p.stdout);assert r['pass'];print(sec,json.dumps(r))
print('Verified',len(manifest),'original remote hashes/full coverage; uploads,4pins,idle',idle)
