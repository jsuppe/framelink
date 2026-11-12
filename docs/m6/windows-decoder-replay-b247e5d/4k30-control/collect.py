from pathlib import Path
import hashlib,json,tarfile
root=Path(__file__).resolve().parent
paths=[]
for folder in (root,root/'prepare',root/'trace-off',root/'trace-on'):
 if folder.exists():paths.extend(p for p in folder.iterdir() if p.is_file() and p.suffix in ('.json','.log','.csv','.py','.ps1'))
manifest=root/'collected-sha256.json'
manifest.write_text(json.dumps({p.relative_to(root).as_posix():hashlib.sha256(p.read_bytes()).hexdigest() for p in paths if p!=manifest},indent=2)+'\n')
if manifest not in paths:paths.append(manifest)
with tarfile.open(root/'evidence.tar','w') as out:
 for p in paths:out.add(p,arcname=p.relative_to(root).as_posix())
