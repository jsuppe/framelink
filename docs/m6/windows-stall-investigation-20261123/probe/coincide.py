import csv,sys,bisect
def load(d):
  S={}
  def rows(n):
    try: return list(csv.DictReader(open(f'{d}/{n}')))
    except FileNotFoundError: return []
  sv=rows('source.video.csv'); sa=rows('source.audio.csv'); ra=rows('relay.audio.csv'); rv=rows('relay.video.csv'); va=rows('receiver.audio.csv'); vv=rows('receiver.video.csv')
  def ser(R,col,base='scheduled_ns'):
    out=[(int(x[base]),(int(x[col])-int(x[base]))/1e6) for x in R if x.get(base) and int(x[base])>0 and x.get(col) and int(x[col])>0]
    out.sort();return out
  S['src.video.wake']=ser(sv,'acquire_begin_ns')
  S['src.video.submit']=[(int(x['scheduled_ns']),(int(x['submitted_ns'])-int(x['submit_begin_ns']))/1e6) for x in sv if int(x.get('submitted_ns') or 0)>0]
  S['src.audio']=ser(sa,'source_ns')
  S['relay.audio']=ser(ra,'arrival_ns')
  S['recv.video']=ser(vv,'arrival_ns')
  S['recv.audio']=ser(va,'arrival_ns')
  # relay video: no schedule; use source schedule by identity
  sched={x['identity']:int(x['scheduled_ns']) for x in sv}
  S['relay.video']=sorted((sched[x['identity']],(int(x['arrival_ns'])-sched[x['identity']])/1e6) for x in rv if x['identity'] in sched)
  return S
def peak(s,t,w=300e6):
  ts=[a for a,_ in s];i=bisect.bisect_left(ts,t-w);j=bisect.bisect_right(ts,t+w)
  return max((v for _,v in s[i:j]),default=None)
for d in sys.argv[1:]:
  S=load(d);print('==',d)
  ev=[]
  for k,s in S.items():
    last=-1e18
    for t,v in s:
      if v>25 and t-last>1e9: ev.append((t,k,v)); last=t
  t0=min(s[0][0] for s in S.values() if s)
  for t,k,v in sorted(ev)[:15]:
    others={kk:(round(peak(ss,t),1) if peak(ss,t) is not None else '-') for kk,ss in S.items()}
    print(f' {(t-t0)/1e9:8.1f}s {k}={v:.1f}  around:',others)
