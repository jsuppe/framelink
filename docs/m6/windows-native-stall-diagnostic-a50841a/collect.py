from pathlib import Path
import hashlib,json,tarfile
root=Path(__file__).resolve().parent
paths=[p for p in root.iterdir() if p.is_file() and p.name not in ('source.tar','evidence.tar','collected-sha256.json','registry.bin')]
for name in ['host','results']:
 if (root/name).exists():paths.extend(p for p in (root/name).rglob('*') if p.is_file())
manifest=root/'collected-sha256.json';manifest.write_text(json.dumps({p.relative_to(root).as_posix():hashlib.sha256(p.read_bytes()).hexdigest() for p in paths},indent=2)+'\n')
with tarfile.open(root/'evidence.tar','w') as t:
 for p in paths+[manifest]:t.add(p,arcname=p.relative_to(root).as_posix())
