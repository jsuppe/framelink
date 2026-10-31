#!/usr/bin/env python3
"""Finite media budget: real peers, independent setup/idle/EOS/cancel bounds."""
import argparse, ctypes, json, os, pathlib, signal, subprocess, tempfile, time
p=argparse.ArgumentParser();p.add_argument('--build',type=pathlib.Path,required=True);p.add_argument('--output',type=pathlib.Path);p.add_argument('--case');a=p.parse_args()
b=a.build.resolve();root=a.output or pathlib.Path(tempfile.mkdtemp(prefix='fl-finite-'));root.mkdir(parents=True,exist_ok=True)
ext='.exe' if os.name=='nt' else '';plugin=str(b/('gstframelink.dll' if ext else 'libgstframelink.so'))
results=[]
def suspend(proc, paused):
    if os.name=='nt':
        call=getattr(ctypes.windll.ntdll,'NtSuspendProcess' if paused else 'NtResumeProcess')
        call.argtypes=[ctypes.c_void_p];call.restype=ctypes.c_long
        assert call(ctypes.c_void_p(int(proc._handle)))==0
    else:os.kill(proc.pid,signal.SIGSTOP if paused else signal.SIGCONT)
for case in ('longer-than-setup','missing-peer','mismatch','legacy-peer','early-eos','source-stall','producer-idle','consumer-idle','cancel'):
    if a.case and a.case!=case:continue
    d=root/case;d.mkdir();session=str(d/'session');started=time.monotonic();children=[];files=[];frozen=None
    common=[plugin,'--session',session,'--frames','200','--timeout-ms','2000','--media-run-ms','70000']
    pc=[str(b/('framelink_av_producer'+ext)),*common]
    cc=[str(b/('framelink_av_consumer'+ext)),*common,'--stream','--clock-policy','producer','--audio-sink','fakesink']
    if case=='mismatch':cc[cc.index('--media-run-ms')+1]='71000'
    if case=='legacy-peer':i=cc.index('--media-run-ms');del cc[i:i+2]
    if case=='early-eos':
        source=d/'source.pipeline';source.write_text('videotestsrc num-buffers=10 ! @VIDEO_SINK@ audiotestsrc num-buffers=10 samplesperbuffer=960 volume=0.0 ! @AUDIO_SINK@\n')
        pc+=['--input-pipeline',str(source)]
    if case=='source-stall':
        source=d/'source.pipeline';source.write_text('videotestsrc num-buffers=200 ! identity sleep-time=3000000 ! @VIDEO_SINK@ audiotestsrc num-buffers=200 samplesperbuffer=960 volume=0.0 ! @AUDIO_SINK@\n')
        pc+=['--input-pipeline',str(source)]
    if case=='cancel':cc=[str(b/('fl_playback_recovery'+ext)),plugin,session,'finite-cancel']
    def launch(cmd,name):
        f=(d/(name+'.log')).open('w');files.append(f)
        proc=subprocess.Popen(cmd,stdout=f,stderr=subprocess.STDOUT);children.append(proc);return proc
    try:
        producer=launch(pc,'producer')
        consumer=None if case=='missing-peer' else launch(cc,'consumer')
        if case.endswith('-idle'):
            # Stop only our child after real media progress; exercise silent peers,
            # not an orderly close or a simulated timeout return value.
            deadline=time.monotonic()+3
            while 'media-progress' not in (d/'producer.log').read_text(errors='replace'):
                assert time.monotonic()<deadline,'no startup progress'
                time.sleep(.02)
            time.sleep(.2)
            frozen=producer if case=='producer-idle' else consumer;suspend(frozen,True)
            live=consumer if frozen is producer else producer
            # The producer can finish this 4 s fixture into the kernel socket
            # buffer while the consumer is stopped; DONE then times out in 2 s.
            # No heartbeat is claimed, and neither wait uses the 70 s budget.
            code=live.wait(timeout=7 if case=='consumer-idle' else 4);assert code!=0
            elapsed=time.monotonic()-started
            suspend(frozen,False);frozen.kill();frozen.wait();frozen=None
        else:
            codes=[q.wait(timeout=max(.1,started+8-time.monotonic())) for q in children]
            elapsed=time.monotonic()-started
            if case=='longer-than-setup':assert codes==[0,0] and elapsed>4,(case,codes,elapsed)
            elif case=='cancel':assert codes[0]!=0 and codes[1]==0,(case,codes)
            else:assert all(code!=0 for code in codes),(case,codes)
            if case!='longer-than-setup':assert elapsed<5,(case,elapsed)
        if case=='source-stall':assert 'media idle deadline' in (d/'producer.log').read_text()
        results.append(dict(case=case,exit_codes=[q.returncode for q in children],elapsed_seconds=elapsed))
        print(json.dumps(results[-1]),flush=True)
    finally:
        if frozen is not None:suspend(frozen,False)
        for q in children:
            if q.poll() is None:q.kill();q.wait()
        for f in files:f.close()
        (root/'results.json').write_text(json.dumps(results,indent=2)+'\n')
for value in ('0','300001','-1','+1',' 1','1ms','18446744073709551616'):
    run=subprocess.run([str(b/('framelink_av_producer'+ext)),plugin,'--session',str(root/'invalid'),'--media-run-ms',value],capture_output=True,text=True,timeout=2)
    assert run.returncode!=0,(value,run.stdout,run.stderr)
print(f'finite budget: {len(results)} real-peer cases and 8 malformed CLI budgets passed')
