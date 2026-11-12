from pathlib import Path
import subprocess,base64,json,hashlib,datetime
out=Path(__file__).parent;src=Path('<linux-home>/dev/vkrender-m6-renderer-baseline')
code=r"""$root='<win-home>/dev/vkrender-m5-final-validation'; $files=@(Get-ChildItem "$root/renderer","$root/common","$root/client","$root/decoder","$root/cmake","$root/test" -Recurse -File);$files+=Get-Item "$root/CMakeLists.txt"; $files | ForEach-Object {@{path=$_.FullName.Substring($root.Length+1).Replace('\','/');sha256=(Get-FileHash $_.FullName).Hash.ToLower()}} | ConvertTo-Json"""
p=subprocess.run(['ssh','-o','BatchMode=yes','<win-user>@<win-host>','powershell','-NoProfile','-EncodedCommand',base64.b64encode(code.encode('utf-16le')).decode()],capture_output=True,check=True,timeout=60);(out/'historical-windows-sha256.json').write_bytes(p.stdout)
rows=json.loads(p.stdout);matches=[];mismatch=[];missing=[]
for row in rows:
 f=src/row['path']
 if not f.exists():missing.append(row['path']);continue
 h=hashlib.sha256(f.read_bytes()).hexdigest()
 (matches if h==row['sha256'] else mismatch).append(row['path'])
result={'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'approved_merge':'9f74b9d1fae7e13d520bc5d4ae9827ac568704f6','historical_windows':'<win-home>/dev/vkrender-m5-final-validation','isolated_source':str(src),'matched':matches,'mismatched':mismatch,'historical_only':missing};(out/'comparison.json').write_text(json.dumps(result,indent=2));print('matches',len(matches),'mismatches',mismatch,'historical_only',missing)
subprocess.run(['git','archive','--format=tar','-o',str(out/'approved-9f74b9d.tar'),'HEAD'],cwd=src,check=True)
(out/'source-sha256.json').write_text(json.dumps({'commit':result['approved_merge'],'tree':subprocess.check_output(['git','rev-parse','HEAD^{tree}'],cwd=src,text=True).strip(),'archive_sha256':hashlib.sha256((out/'approved-9f74b9d.tar').read_bytes()).hexdigest(),'files':{n:hashlib.sha256((src/n).read_bytes()).hexdigest() for n in subprocess.check_output(['git','ls-files'],cwd=src,text=True).splitlines() if (src/n).is_file()}},indent=2));print('isolated Git source and tar pinned')
