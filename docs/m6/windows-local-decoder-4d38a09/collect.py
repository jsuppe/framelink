"""Archive reports only, excluding source, build products and discovery credentials."""
from pathlib import Path
import tarfile
root=Path(__file__).resolve().parent
paths=[]
for folder in (root,root/'smoke'):
 if folder.exists():paths.extend(p for p in folder.iterdir() if p.is_file() and p.suffix in ('.json','.log','.csv'))
with tarfile.open(root/'evidence.tar','w') as out:
 for path in paths:out.add(path,arcname=path.relative_to(root).as_posix())
print('archived report files',len(paths))
