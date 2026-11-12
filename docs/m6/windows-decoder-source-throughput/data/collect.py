from pathlib import Path
import hashlib,json,tarfile
p=Path(__file__).resolve().parent
files=[f for f in p.rglob('*') if f.is_file() and f.name not in {'evidence.tar','collected-sha256.json'} and '__pycache__' not in f.parts]
(p/'collected-sha256.json').write_text(json.dumps({f.relative_to(p).as_posix():hashlib.sha256(f.read_bytes()).hexdigest() for f in files},indent=2)+'\n')
with tarfile.open(p/'evidence.tar','w') as t:
 for f in files+[p/'collected-sha256.json']:t.add(f,arcname=f.relative_to(p).as_posix())
