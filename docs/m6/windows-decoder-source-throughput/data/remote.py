"""Run only from execute.ps1 while holding the exclusive Windows runtime lane."""
import datetime, hashlib, json, os, pathlib, subprocess, sys, time
root=pathlib.Path(__file__).resolve().parent
out=root/'results'
out.mkdir(exist_ok=False)
gst=pathlib.Path('C:/Program Files/gstreamer/1.0/msvc_x86_64')
launch=gst/'bin/gst-launch-1.0.exe'
state={'state':'preparing','started_utc':datetime.datetime.now(datetime.timezone.utc).isoformat(), 'mode':'clean then instrumented counts','requested_frames':150,'scope':'upstream synthetic source throughput; no decoder/relay qualification'}
def save(): (out/'status.json').write_text(json.dumps(state,indent=2)+'\n')
def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
try:
    env=os.environ.copy()
    for k in list(env):
        if k.startswith(('GST_','FL_','FRAMELINK_')): env.pop(k,None)
    env['PATH']=str(gst/'bin')+';'+env['PATH']
    env['GST_REGISTRY']=str(out/'registry.bin')
    state['pins']={str(p):digest(p) for p in [launch,*sorted((gst/'bin').glob('*gstreamer*.dll')),*sorted((gst/'lib/gstreamer-1.0').glob('*videotestsrc*.dll')),*sorted((gst/'lib/gstreamer-1.0').glob('*x264*.dll')),*sorted((gst/'lib/gstreamer-1.0').glob('*videoparsers*.dll')),*sorted((gst/'lib/gstreamer-1.0').glob('*coreelements*.dll')),*sorted((gst/'lib/gstreamer-1.0').glob('*coretracers*.dll'))]}
    save()
    # Prewarm only the plugin registry, not a media pipeline. Startup remains in wall elapsed.
    subprocess.run([str(gst/'bin/gst-inspect-1.0.exe'),'--version'],env=env,stdout=(out/'version.log').open('w'),stderr=subprocess.STDOUT,check=True,timeout=30)
    source=['videotestsrc','num-buffers=150','pattern=snow','!','video/x-raw,format=NV12,width=3840,height=2160,framerate=30/1']
    encode=['!','x264enc','tune=zerolatency','speed-preset=ultrafast','key-int-max=30','!','h264parse','!','video/x-h264,alignment=au']
    for arm,stage,instrumented in [('gen-clean',[],False),('encode-clean',encode,False),('gen-count',[],True),('encode-count',encode,True)]:
        probe=['!','identity','name=probe','silent=false'] if instrumented else []
        command=[str(launch),'-v' if instrumented else '-q',*source,*stage,*probe,'!','fakesink','sync=false','async=false']
        arm_env=env.copy()
        if instrumented: arm_env.update(GST_TRACERS='stats',GST_DEBUG='GST_TRACER:7',GST_DEBUG_NO_COLOR='1',GST_DEBUG_FILE=str(out/(arm+'.trace.log')))
        state['current_arm']=arm;state[arm]={'command':command};save()
        start=time.perf_counter_ns()
        with (out/(arm+'.log')).open('w') as log:
            p=subprocess.Popen(command,env=arm_env,stdout=log,stderr=subprocess.STDOUT)
            state['owned_pid']=p.pid;save()
            try: rc=p.wait(timeout=90)
            except subprocess.TimeoutExpired:
                subprocess.run(['taskkill','/PID',str(p.pid),'/T','/F'],stdout=log,stderr=subprocess.STDOUT,timeout=20)
                p.wait(timeout=20);raise RuntimeError(arm+' timed out; only owned process tree stopped')
        state.pop('owned_pid',None)
        state[arm].update(exit_code=rc,wall_ns=time.perf_counter_ns()-start);save()
        if rc: raise RuntimeError(arm+' failed; no next arm')
    state['state']='finished'
except Exception as e:state.update(state='failed',error=str(e))
finally:
    state['ended_utc']=datetime.datetime.now(datetime.timezone.utc).isoformat();save()
    (out/'sha256.json').write_text(json.dumps({p.name:digest(p) for p in out.iterdir() if p.is_file() and p.name!='sha256.json'},indent=2)+'\n')
sys.exit(0 if state['state']=='finished' else 1)
