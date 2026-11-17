import pathlib,subprocess,json,csv,hashlib
root=pathlib.Path(__file__).parent
repo=pathlib.Path('<linux-home>/dev/framelink-m6');build=pathlib.Path('<linux-home>/dev/build')
results=[]
def run(label,args,timeout=240):
 with (root/(label+'.log')).open('w') as f:r=subprocess.run(args,stdout=f,stderr=subprocess.STDOUT,timeout=timeout)
 results.append(dict(label=label,args=args,exit=r.returncode));(root/'results.json').write_text(json.dumps(results,indent=2));print(label,r.returncode,flush=True)
 assert r.returncode==0,label
for abi in ['linux','arm64','x86_64']:
 run('build-'+abi,['cmake','--build',str(build/('framelink-m6-'+abi)),'-j4','--target','fl_gst_av_bench','fl_gst_av_bin'])
run('linux-regression',['ctest','--test-dir',str(build/'framelink-m6-linux'),'-R','^(gst_av_bin|gst_av_bench_smoke|gst_shared_cpu_av)$','--output-on-failure'])
args=['--shared-cpu','--seconds','5','--size','64x64','--fps','30','--latency-ms','0','--transport-only','--no-output-preroll','--pool','4','--video-queue','2','--raw','--trace']
for fmt in ['BGRA','NV12']:
 run('linux-'+fmt.lower(),[str(build/'framelink-m6-linux/fl_gst_av_bench'),str(build/'framelink-m6-linux/libgstframelink.so')]+args+['--format',fmt,'--out',str(root),'--label','linux-'+fmt.lower()])
adb=['<linux-home>/Android/Sdk/platform-tools/adb','-s','A06B4AABFB07983'];remote='/data/local/tmp/framelink-cpu-bench'
run('android-mkdir',adb+['shell','mkdir -p '+remote])
run('android-push',adb+['push',str(build/'framelink-m6-arm64/fl_gst_av_bench'),remote+'/'])
run('android-chmod',adb+['shell','chmod 755 '+remote+'/fl_gst_av_bench'])
for fmt in ['BGRA','NV12']:
 run('android-'+fmt.lower(),adb+['shell','cd '+remote+' && ./fl_gst_av_bench static '+' '.join(args+['--format',fmt,'--out',remote,'--label','android-'+fmt.lower()])])
run('android-pull',adb+['pull',remote,str(root/'android')])
report={}
for platform,folder in [('linux',root),('android',root/'android')]:
 for fmt in ['bgra','nv12']:
  label=platform+'-'+fmt
  summary=dict(line.split('=',1) for line in (folder/(label+'.summary.txt')).read_text().splitlines())
  assert summary['complete']=='yes' and summary['consumer.frames']=='150' and summary['consumer.samples']=='240000'
  counts={}
  for role,op in [('producer','gst.shared_cpu_copy_sink'),('consumer','gst.shared_cpu_copy_source')]:
   files=list(folder.glob(label+'-'+role+'-gst.*.csv'));assert len(files)==1,files
   rows=list(csv.DictReader(x for x in files[0].read_text().splitlines() if not x.startswith('#')))
   selected=[r for r in rows if r['operation']==op]
   total=sum(int(r['payload_bytes']) for r in selected)
   assert len(selected)==150 and total==150*64*64*(4 if fmt=='bgra' else 1.5),(label,op,len(selected),total)
   assert all(int(r['result'])==0 and int(r['operations'])==1 for r in selected)
   counts[role]={'copies':len(selected),'bytes':total}
  report[label]=counts
(root/'copy-accounting.json').write_text(json.dumps(report,indent=2))
(root/'source-sha256.json').write_text(json.dumps({p:hashlib.sha256((repo/p).read_bytes()).hexdigest() for p in ['test/fl_gst_av_bench.cpp','test/fl_gst_av_harness.h']},indent=2))
print('counts and exact copy accounting passed',flush=True)
