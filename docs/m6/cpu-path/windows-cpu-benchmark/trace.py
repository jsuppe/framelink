from pathlib import Path
import subprocess,os,json,csv
root=Path(__file__).parent/'cpu-trace';root.mkdir(exist_ok=True)
build=Path(__file__).parent/'build'/'Release'
env=dict(os.environ);env['PATH']=str(build)+';C:/Program Files/gstreamer/1.0/msvc_x86_64/bin;'+env['PATH']
args=[str(build/'fl_gst_av_bench.exe'),str(build/'gstframelink.dll'),'--shared-cpu','--seconds','5','--size','64x64','--fps','30','--latency-ms','0','--transport-only','--no-output-preroll','--pool','2','--video-queue','1','--raw','--trace']
results={}
for fmt in ['BGRA','NV12']:
 label=fmt.lower();cmd=args+['--format',fmt,'--out',root.as_posix(),'--label',label]
 with (root/(label+'.log')).open('w') as f:r=subprocess.run(cmd,env=env,stdout=f,stderr=subprocess.STDOUT,timeout=60)
 assert r.returncode==0,(fmt,r.returncode)
 summary=dict(line.split('=',1) for line in (root/(label+'.summary.txt')).read_text().splitlines())
 assert summary['complete']=='yes' and summary['consumer.frames']=='150' and summary['consumer.samples']=='240000'
 counts={}
 for role,op in [('producer','gst.shared_cpu_copy_sink'),('consumer','gst.shared_cpu_copy_source')]:
  files=list(root.glob(label+'-'+role+'-gst.*.csv'));assert len(files)==1,files
  rows=list(csv.DictReader(x for x in files[0].read_text().splitlines() if not x.startswith('#')))
  selected=[r for r in rows if r['operation']==op];total=sum(int(r['payload_bytes']) for r in selected)
  assert len(selected)==150 and total==150*64*64*(4 if fmt=='BGRA' else 1.5),(fmt,op,len(selected),total)
  assert all(int(r['result'])==0 and int(r['operations'])==1 for r in selected)
  counts[role]={'copies':len(selected),'bytes':total}
 results[fmt]=counts
(root/'results.json').write_text(json.dumps(results,indent=2));print(results)
