import sys,collections
f,tid,a,b=sys.argv[1],sys.argv[2],int(sys.argv[3]),int(sys.argv[4])
mods=collections.Counter();paths=collections.Counter();cur=None;frames=[];n=0
def flush():
  global n
  if cur is None: return
  n+=1;ms=[x.split('!')[0] for x in frames]
  for m in dict.fromkeys(ms): mods[m]+=1
  seq=[];[seq.append(m) for m in reversed(ms) if not seq or seq[-1]!=m];paths[' > '.join(seq)]+=1
for line in open(f,errors='replace'):
  s=line.lstrip()
  if s.startswith('SampledProfile,'):
    flush();cur=None;frames=[];p=[x.strip() for x in s.split(',')]
    if p[3]==tid and a<=int(p[1])<=b: cur=p[1]
  elif s.startswith('Stack,'):
    if cur:
      p=[x.strip() for x in s.split(',')]
      if p[1]==cur and p[2]==tid: frames.append(p[5])
  elif cur: flush();cur=None;frames=[]
flush();print('samples',n,mods.most_common(14))
for k,v in paths.most_common(5): print(' ',v,k)
