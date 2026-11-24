import sys,collections
f,tid,a,b=sys.argv[1],sys.argv[2],int(sys.argv[3]),int(sys.argv[4])
preempt=collections.Counter();readywait=0;offwait=0
for line in open(f,errors='replace'):
  s=line.lstrip()
  if not s.startswith('CSwitch,'): continue
  p=[x.strip() for x in s.split(',')]
  try: ts=int(p[1])
  except: continue
  if not(a<=ts<=b): continue
  if p[3]==tid: print(f'{ts/1000:.3f} IN  cpu{p[16]} after {int(p[7])/1000:.2f} ms (replacing {p[8]} {p[9]})')
  if p[9]==tid:
    print(f'{ts/1000:.3f} OUT cpu{p[16]} ran {int(p[15])/1000:.2f} ms -> {p[12]}/{p[13]}; next on cpu: {p[2]} tid {p[3]} pri {p[4]}')
    if p[12]=='Ready': preempt[p[2]]+=1
print('preempted by:',preempt.most_common(8))
