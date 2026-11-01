import subprocess,json,pathlib,time,sys
start=time.monotonic();reference=None
for n in range(50):
 r=subprocess.run([sys.argv[1]],capture_output=True,text=True,timeout=5)
 assert r.returncode==0,(n,r.stdout,r.stderr)
 if reference is None:reference=r.stdout
 assert r.stdout==reference,(n,r.stdout,reference)
out=pathlib.Path(sys.argv[2]);out.write_text(json.dumps(dict(runs=50,passed=50,identical_output=True,seconds=time.monotonic()-start,output=reference),indent=2)+'\n');print(out.read_text())
