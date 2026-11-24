import sys,collections
f,tid0=sys.argv[1],sys.argv[2]
cs=[];rdy=[];samp=collections.defaultdict(list)
for line in open(f,errors='replace'):
  s=line.lstrip()
  if s.startswith('CSwitch,'):
    p=[x.strip() for x in s.split(',')]
    try: cs.append((int(p[1]),p[2],p[3],p[8],p[9],p[12],p[13]))
    except: pass
  elif s.startswith('ReadyThread,'):
    p=[x.strip() for x in s.split(',')]
    try: rdy.append((int(p[1]),p[2],p[3],p[4],p[5]))
    except: pass
  elif s.startswith('SampledProfile,'):
    p=[x.strip() for x in s.split(',')]
    try: samp[p[3]].append((int(p[1]),p[7].split('!')[0]))
    except: pass
def longest_wait(tid):
  out=[(ts,r,st) for ts,_,_,op,ot,st,r in cs if ot==tid]
  ins=[ts for ts,np_,nt,*_ in cs if nt==tid]
  best=None
  for ts,r,st in out:
    nxt=[i for i in ins if i>ts]
    if nxt and st=='Waiting':
      d=nxt[0]-ts
      if not best or d>best[0]: best=(d,ts,nxt[0],r)
  return best
tid=tid0;seen=set()
for depth in range(5):
  w=longest_wait(tid)
  if not w: break
  d,a,b,r=w
  waker=[x for x in rdy if x[4]==tid and a<x[0]<=b]
  wk=waker[-1] if waker else None
  print(f'tid {tid}: waited {d/1000:.1f} ms ({r}) from {a/1000:.3f}; woken by {wk[1]} tid {wk[2]}' if wk else f'tid {tid}: waited {d/1000:.1f} ms ({r}); waker unknown')
  if not wk or wk[2] in seen or wk[2]=='0': break
  seen.add(tid);tid=wk[2]
  ss=collections.Counter(m for t,m in samp[tid] if a<=t<=b)
  if ss: print(f'   {tid} samples during that wait: {dict(ss.most_common(5))}')
