import os,sys,pathlib,subprocess,threading,json,runpy,time,hashlib
root=pathlib.Path(__file__).resolve().parent
repo=root.parent
build=repo/'build/Release'
results=[]
original=subprocess.Popen
class RamOutput(original):
    def __init__(self,*args,**kwargs):
        self.saved=kwargs.get('stdout'); self.data=None
        capture=self.saved is not None and hasattr(self.saved,'write')
        if capture:kwargs['stdout']=subprocess.PIPE
        super().__init__(*args,**kwargs)
        self.reader=None
        if capture:
            def read():self.data=self.stdout.read()
            self.reader=threading.Thread(target=read);self.reader.start()
    def wait(self,*args,**kwargs):
        code=super().wait(*args,**kwargs)
        if self.reader:
            self.reader.join(); self.saved.write(self.data.decode());self.saved.flush();self.reader=None
        return code
manifest={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in build.iterdir() if p.suffix in ('.dll','.exe')}
(root/'binary-manifest.json').write_text(json.dumps(manifest,indent=2))
for policy in ('producer','audio-coordinated'):
 for output in ('file','ram'):
    out=root/(policy+'-'+output);out.mkdir(exist_ok=False)
    os.environ['FRAMELINK_GST_TRACE']=str(out/'gst')
    os.environ['FRAMELINK_TRACE']=str(out/'transport')
    os.environ['FRAMELINK_TRACE_EVENTS']='65536'
    os.environ['FRAMELINK_TEST_PRODUCER_OPEN_DELAY_MS']='350'
    subprocess.Popen=RamOutput if output=='ram' else original
    sys.argv=[str(repo/'test/m5_external_pipeline.py'),'--build',str(build),'--output',str(out),'--clock-policy',policy]
    error=None
    try:runpy.run_path(sys.argv[0],run_name='__main__')
    except Exception as e:error=repr(e)
    finally:subprocess.Popen=original
    cells=[]
    for fmt in ('BGRA','NV12'):
      p=out/(fmt+'-consumer.log')
      rows=[json.loads(x) for x in p.read_text().splitlines() if x.startswith('{')] if p.exists() else []
      received=[r['pts_ns'] for r in rows if r.get('event')=='video-schedule' and r.get('stage')=='received']
      media={r['pts_ns']:r['monotonic_ns'] for r in rows if r.get('event')=='media-trace' and r.get('track')=='video'}
      delays=[(r['monotonic_ns']-media[r['pts_ns']])/1e6 for r in rows if r.get('event')=='video-schedule' and r.get('stage')=='received' and r['pts_ns'] in media]
      cells.append(dict(format=fmt,count=len(received),missing=sorted(set(range(0,2000000000,20000000))-set(received)),probe_to_pull_max_ms=max(delays,default=0)))
    results.append(dict(policy=policy,stdout=output,error=error,cells=cells))
    (root/'summary.json').write_text(json.dumps(results,indent=2))
print(json.dumps(results,indent=2))
