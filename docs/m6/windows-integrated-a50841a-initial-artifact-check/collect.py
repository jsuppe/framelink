from pathlib import Path
import hashlib,json,tarfile
root=Path(__file__).resolve().parent
files=[]
for name in ['results','host']:
 p=root/name
 if p.exists():files.extend(f for f in p.rglob('*') if f.is_file())
files.extend(root/n for n in ['config.json','references.json','artifact-sha256.json','remote.py','execute.ps1','verify.py','collect.py'] if (root/n).is_file())
manifest={p.relative_to(root).as_posix():hashlib.sha256(p.read_bytes()).hexdigest() for p in files}
(root/'collected-sha256.json').write_text(json.dumps(manifest,indent=2)+'\n')
with tarfile.open(root/'evidence.tar','w') as t:
 for p in files+[root/'collected-sha256.json']:t.add(p,arcname=p.relative_to(root).as_posix())
print('Collected files',len(files))
