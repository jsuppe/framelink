"""Function-level stacks of one thread's CPU samples in a symbolized xperf dumper window.
Usage: stkf.py <dump.txt> <tid> <from_us> <to_us> [depth]
Prints the sample count, the most common functions (each counted once per sample)
and the most common root-to-leaf paths, truncated to the outermost `depth` frames
(default: whole path, consecutive duplicates removed)."""
import sys,collections
f,tid,a,b=sys.argv[1],sys.argv[2],int(sys.argv[3]),int(sys.argv[4]);depth=int(sys.argv[5]) if len(sys.argv)>5 else 0
funcs=collections.Counter();paths=collections.Counter();cur=None;frames=[];n=0
def flush():
  global n
  if cur is None: return
  n+=1
  for x in dict.fromkeys(frames): funcs[x]+=1
  seq=[];[seq.append(x) for x in reversed(frames) if not seq or seq[-1]!=x]
  paths[' > '.join(seq[:depth] if depth else seq)]+=1
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
flush();print('samples',n)
for k,v in funcs.most_common(40): print(f'{v:5d} {k}')
print('paths:')
for k,v in paths.most_common(6): print(f'{v:5d} {k}')
