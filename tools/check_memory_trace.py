#!/usr/bin/env python3
"""Check the native-producer or 12-frame padded-NV12 diagnostic fixture trace.
This is FrameLink boundary instrumentation, not a driver-wide zero-copy proof.
"""
import argparse, csv, re
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('mode',choices=['native','nv12'])
p.add_argument('file')
p.add_argument('--windows',action='store_true')
a=p.parse_args()
lines=open(a.file,encoding='utf-8').readlines()
loss=[re.search(r'dropped_events=(\d+),rejected_threads=(\d+)',line) for line in lines]
loss=[m for m in loss if m]
assert len(loss)==1 and loss[0].groups()==('0','0'),'missing or lossy recorder footer'
rows=list(csv.DictReader(line for line in lines if not line.startswith('#')))
assert rows and all(None not in r for r in rows),'empty/malformed trace'
payload=[r for r in rows if int(r.get('details','0'))&1]
if a.mode=='native':
    assert sum(r['operation']=='gst.native_frame_submitted' for r in rows)>=1,'no native positive control'
    assert not payload,'native boundary issued unexpected payload operations'
else:
    expected=12*130*66*3//2
    operations=['gst.cpu_copy_source']
    operations+=['gst.cpu_repack','gst.gpu_upload_issued','gst.gpu_readback_issued'] if a.windows else ['gst.cpu_copy_sink']
    for op in operations:
        observed=sum(int(r['payload_bytes']) for r in payload if r['operation']==op)
        assert observed==expected,(op,observed,expected)
    assert len(set(r['operation'] for r in payload))==len(operations),'unexpected payload operation'
print('PASS',a.mode,'observed boundary payload accounting; no driver/scanout claim')
