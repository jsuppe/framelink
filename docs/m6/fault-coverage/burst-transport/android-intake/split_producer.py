import csv,sys,statistics as st
def load(p):
    rows=[r for r in csv.DictReader(l for l in open(p) if not l.startswith('#'))]
    by={}
    for r in rows: by.setdefault(r['operation'],[]).append(r)
    for v in by.values(): v.sort(key=lambda r:int(r['start_ns']))
    return by
for core,gst in zip(sys.argv[1::2],sys.argv[2::2]):
    c=load(core); g=load(gst)
    ops=['flAcquireBuffer','flImageMap','flSubmit']
    unmap=c['flImageUnmap']
    # gst events: receive, acquired, copy(end marker), render (start,duration)
    rec=g['gst.frame_receive']; acq=g['gst.frame_acquired']; cp=g['gst.cpu_copy_sink']; ren=g['gst.sinkRender']
    def ms(x):return x/1e6
    out={}
    for name,rng in (('steady',range(120,340)),('drain',range(360,382))):
        d={k:[] for k in ['render','wait(recv->acq)','acquire_call','copy(acq->copyend)','map_call','submit_call','gap_to_next_recv']}
        for i in rng:
            s=int(ren[i]['start_ns']);d['render'].append(ms(int(ren[i]['duration_ns'])))
            d['wait(recv->acq)'].append(ms(int(acq[i]['start_ns'])-int(rec[i]['start_ns'])))
            d['acquire_call'].append(ms(int(c['flAcquireBuffer'][i]['duration_ns'])))
            d['copy(acq->copyend)'].append(ms(int(cp[i]['start_ns'])-int(acq[i]['start_ns'])))
            d['map_call'].append(ms(int(c['flImageMap'][i]['duration_ns'])))
            d['submit_call'].append(ms(int(c['flSubmit'][i]['duration_ns'])))
            d['gap_to_next_recv'].append(ms(int(rec[i+1]['start_ns'])-(s+int(ren[i]['duration_ns']))))
        print(core.split('-producer')[0],name,' '.join(f"{k}={st.median(v):.2f}/{max(v):.2f}" for k,v in d.items()))
