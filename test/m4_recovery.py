#!/usr/bin/env python3
"""Actual authenticated peers: bounded retry, pause/session reset, fault recovery."""
import argparse, os, pathlib, subprocess, tempfile, time
p=argparse.ArgumentParser();p.add_argument('--build',required=True);a=p.parse_args()
b=pathlib.Path(a.build).resolve();ext='.exe' if os.name=='nt' else ''
plugin=b/('gstframelink.dll' if os.name=='nt' else 'libgstframelink.so')
for mode in ('bounded','normal','pause','recover'):
    with tempfile.TemporaryDirectory(prefix='fl-m4-recovery-') as root:
        session=str(pathlib.Path(root)/'session'); procs=[]
        def producer(fault=None,sessions=1):
            cmd=[str(b/('framelink_av_producer'+ext)),str(plugin),'--session',session,'--frames','75','--timeout-ms','12000','--sessions',str(sessions)]
            if fault:cmd+=['--fault',fault]
            out=open(pathlib.Path(root)/('producer-'+str(len(procs))+'.log'),'w')
            proc=subprocess.Popen(cmd,stdout=out,stderr=out);procs.append((proc,out));return proc
        try:
            if mode!='bounded':producer('audio-pts-reset' if mode=='recover' else None,2 if mode=='pause' else 1)
            consumer=subprocess.Popen([str(b/('fl_playback_recovery'+ext)),str(plugin),session,mode],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
            if mode=='recover':
                procs[0][0].wait(timeout=10)
                producer()
            output=consumer.communicate(timeout=20)[0]
            print(mode,output.strip(),flush=True)
            if consumer.returncode:
                for log in pathlib.Path(root).glob('*.log'):print(log.read_text(),flush=True)
            assert consumer.returncode==0,(mode,output)
        finally:
            for proc,out in procs:
                if proc.poll() is None:proc.terminate()
                proc.wait(timeout=5);out.close()
print('4 recovery scenarios passed')
