from pathlib import Path
import subprocess,base64,json,hashlib,math
root=Path(__file__).resolve().parent;data=root/'data';remote=json.loads((root/'controller-status.json').read_text())['remote']
c=f"$r='{remote}';Get-ChildItem $r -Recurse -File | ForEach-Object {{@{{path=$_.FullName.Substring($r.Length+1).Replace('\\','/');sha256=(Get-FileHash $_.FullName).Hash.ToLowerInvariant()}}}} | ConvertTo-Json -Depth 4"
p=subprocess.run(['ssh','-o','BatchMode=yes','<win-user>@<win-host>','powershell','-NoProfile','-EncodedCommand',base64.b64encode(c.encode('utf-16le')).decode()],capture_output=True,text=True,timeout=60);assert p.returncode==0;rows=json.loads(p.stdout);(root/'remote-sha256.json').write_text(json.dumps(rows,indent=2)+'\n')
for row in rows:assert hashlib.sha256((data/row['path']).read_bytes()).hexdigest()==row['sha256'],row['path']
assert {str(p.relative_to(data)) for p in data.rglob('*') if p.is_file()}=={r['path'] for r in rows}
s=json.loads((data/'status.json').read_text());assert s['state']=='finished';assert json.loads((data/'idle-final.json').read_text())['count']==0
for case in s['cases']:
 folder=data/case['label'];n=int(case['label'].rsplit('-',1)[1]);long=dict(x.split('=',1) for x in (folder/(case['label']+'-long.txt')).read_text().splitlines() if '=' in x)
 for i in range(n):
  x=long['cycle.'+str(i)].split('|');assert x[:3]==['EOS','300','240000']
  fresh=dict(x.split('=',1) for x in (folder/(case['label']+'-fresh-'+str(i)+'.txt')).read_text().splitlines() if '=' in x)
  assert fresh['result']=='EOS' and fresh['pushed.frames']=='300' and fresh['pushed.samples']=='240000'
 if n==100:
  values=[[i]+[float(y) for y in long['cycle.'+str(i)].split('|')[3:]] for i in range(5,n)]
  meanx=sum(x[0] for x in values)/len(values);den=sum((x[0]-meanx)**2 for x in values)
  for j,key,limit in [(1,'memory_kib_per_cycle',16),(2,'handles_per_cycle',.1),(3,'threads_per_cycle',.02)]:
   meany=sum(x[j] for x in values)/len(values);slope=sum((x[0]-meanx)*(x[j]-meany) for x in values)/den
   assert slope<=limit and abs(slope-case['slopes'][key])<1e-9
result={'pass':True,'remote_hashes':len(rows),'full_coverage':True,'cycles_verified':102,'video_frames':30600,'pcm_frames':24480000,'idle':0,'scope':'counts/production shared-CPU lifecycle resources only; native failure unchanged'};(root/'independent-collection.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))
