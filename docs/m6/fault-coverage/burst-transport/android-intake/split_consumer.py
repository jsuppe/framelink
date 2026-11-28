import csv,sys,statistics as st
def load(p):
    by={}
    for r in csv.DictReader(l for l in open(p) if not l.startswith('#')): by.setdefault(r['operation'],[]).append(r)
    for v in by.values(): v.sort(key=lambda r:int(r['start_ns']))
    return by
for pc,cc in zip(sys.argv[1::2],sys.argv[2::2]):
    p=load(pc);c=load(cc)
    # steady clock is shared across processes on one host (CLOCK_MONOTONIC)
    sub=p['flSubmit'];acq=p['flAcquireBuffer'];rx=c['ipc.frame_received'];ra=c['reader.acquire'];rc=c['reader.complete'];rr=c['reader.retire']
    for name,rng in (('steady',range(120,340)),('drain',range(360,382))):
        d={k:[] for k in ['submit->recv','recv->acquire','acquire->complete','complete->retire','slot_held(recv->retire)','retire_interval']}
        for i in rng:
            s=int(sub[i]['start_ns'])+int(sub[i]['duration_ns']);r=int(rx[i]['start_ns'])
            d['submit->recv'].append((r-s)/1e6);d['recv->acquire'].append((int(ra[i]['start_ns'])-r)/1e6)
            d['acquire->complete'].append((int(rc[i]['start_ns'])-int(ra[i]['start_ns']))/1e6)
            d['complete->retire'].append((int(rr[i]['start_ns'])-int(rc[i]['start_ns']))/1e6)
            d['slot_held(recv->retire)'].append((int(rr[i]['start_ns'])-r)/1e6)
            d['retire_interval'].append((int(rr[i+1]['start_ns'])-int(rr[i]['start_ns']))/1e6)
        print(pc.split('-producer')[0],name,' '.join(f"{k}={st.median(v):.2f}/{max(v):.2f}" for k,v in d.items()))
