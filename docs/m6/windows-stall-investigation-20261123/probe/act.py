import sys,collections
f,t,dur=sys.argv[1],int(sys.argv[2]),int(float(sys.argv[3])*1000)
a,b=t,t+dur
ins=collections.Counter();oncpu=collections.Counter();reasons=collections.defaultdict(collections.Counter);proc={}
samp=collections.defaultdict(collections.Counter);last={}
for line in open(f,errors='replace'):
  s=line.lstrip()
  if s.startswith('CSwitch,'):
    p=[x.strip() for x in s.split(',')]
    try: ts=int(p[1])
    except: continue
    if not(a<=ts<=b): continue
    if 'fl_gst_local' in p[2]: ins[p[3]]+=1;proc[p[3]]=p[2]
    if 'fl_gst_local' in p[8]: oncpu[p[9]]+=int(p[15]);reasons[p[9]][p[12]+'/'+p[13]]+=1;proc[p[9]]=p[8]
  elif s.startswith('SampledProfile,'):
    p=[x.strip() for x in s.split(',')]
    try: ts=int(p[1])
    except: continue
    if a<=ts<=b and 'fl_gst_local' in p[2]: samp[p[3]][p[7].split('!')[0]]+=1;proc[p[3]]=p[2]
tids=set(ins)|set(oncpu)|set(samp)
for tid in sorted(tids,key=lambda x:-oncpu[x]-1000*sum(samp[x].values()))[:6]:
  print(f' tid {tid} {proc[tid]}: switch-ins {ins[tid]}, on-cpu {oncpu[tid]/1000:.1f} ms, out-reasons {dict(reasons[tid].most_common(4))}, samples {dict(samp[tid].most_common(6))}')
