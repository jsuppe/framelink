#!/usr/bin/env python3
"""Count application receipts, not just appsink pad arrivals. No display claim."""
import argparse,json,os,pathlib,subprocess,sys,tempfile
p=argparse.ArgumentParser();p.add_argument('--build',required=True);p.add_argument('--output');a=p.parse_args()
root=pathlib.Path(a.output or tempfile.mkdtemp(prefix='m5-drain-')).resolve();root.mkdir(parents=True,exist_ok=True)
results=[]
for case,delay,stall in [('startup',700,0),('steady',0,0),('stall',0,500),('restart',700,0)]:
    out=root/case
    env=dict(os.environ,FRAMELINK_TEST_PREROLL_DELAY_MS=str(delay),FRAMELINK_TEST_PRESENT_STALL_MS=str(stall))
    run=subprocess.run([sys.executable,str(pathlib.Path(__file__).with_name('m5_external_pipeline.py')),'--build',a.build,'--output',str(out),'--clock-policy','producer'],env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,timeout=60)
    (root/(case+'.log')).write_text(run.stdout)
    # The external helper also checks payload hashes and unchanged audio PTS.
    assert run.returncode==0,(case,run.stdout)
    for fmt in ('BGRA','NV12'):
        rows=[json.loads(s) for s in (out/(fmt+'-consumer.log')).read_text().splitlines() if s.startswith('{')]
        events=[r for r in rows if r.get('event')=='video-schedule']
        received=[r['pts_ns'] for r in events if r['stage']=='received']
        terminal=[r['pts_ns'] for r in events if r['stage'] in ('scheduled','late','queue-overflow')]
        expected=list(range(0,2_000_000_000,20_000_000))
        assert received==expected,(case,fmt,len(received),received)
        assert sorted(terminal)==expected,(case,fmt,'missing/duplicate disposition')
        summary=next(r for r in rows if r.get('event')=='video-schedule-summary')
        assert summary['received']==summary['scheduled']+summary['late']+summary['overflow']==100,summary
        assert summary['peak_pending']<=summary['pending_limit'] and summary['peak_bytes']<=summary['byte_limit'],summary
        if not stall:assert summary['scheduled']==100 and summary['late']==summary['overflow']==0,summary
        else:assert summary['late']+summary['overflow']>0,('stall negative control not detected',summary)
        results.append(dict(case=case,format=fmt,**summary))
(root/'summary.json').write_text(json.dumps(results,indent=2)+'\n');print(json.dumps(results,indent=2))
