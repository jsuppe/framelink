import subprocess,pathlib,json
root=pathlib.Path(__file__).parent
base=['<linux-home>/dev/build/framelink-m6-linux/fl_gst_av_bench','<linux-home>/dev/build/framelink-m6-linux/libgstframelink.so','--shared-cpu']
results=[]
for i,opts in enumerate([['--native'],['--mode','element'],['--source-only'],['--local-consumer'],['--cycles','100']]):
 r=subprocess.run(base+opts,capture_output=True,text=True,timeout=5)
 assert r.returncode!=0 and '!gSharedCpu' in r.stderr,(opts,r)
 (root/f'negative-{i}.log').write_text(r.stdout+r.stderr)
 results.append({'args':opts,'exit':r.returncode})
(root/'negative.json').write_text(json.dumps(results,indent=2))
