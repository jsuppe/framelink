"""Outstanding (never freed) heap allocations and VirtualAlloc commits of one
process in a symbolized xperf dumper file, grouped by allocation stack.
Usage: outstanding.py dump.txt pid [--times]
--times also lists each allocation's size and trace time for groups of 3 or more."""
import sys,collections
f,pid=sys.argv[1],sys.argv[2];times='--times' in sys.argv[3:];tag=f'({pid})'
live={}           # (heap,addr) -> (size, stackkey, ts)
vlive={}          # base -> (size, stackkey, ts)
stacks={};cur=None;frames=[]
SKIP=('ntdll.dll!Rtlp','ntdll.dll!RtlAllocateHeap','ntdll.dll!RtlReAllocateHeap','KernelBase.dll!HeapAlloc','ucrtbase.dll!_malloc_base','ucrtbase.dll!_calloc_base','ucrtbase.dll!_realloc_base','ntoskrnl.exe!','ntdll.dll!NtAllocateVirtualMemory','KernelBase.dll!VirtualAlloc','ntdll.dll!ZwAllocate')
def close():
  global cur,frames
  if cur: stacks[cur]=tuple(x for x in frames if not x.startswith(SKIP))[:14]
  cur=None;frames=[]
for line in open(f,errors='replace'):
  s=line.lstrip()
  if s.startswith('Stack,'):
    if cur:
      p=[x.strip() for x in s.split(',',5)]
      frames.append(p[5])
    continue
  close()
  if tag not in s: continue
  p=[x.strip() for x in s.split(',')]
  ev=p[0]
  if ev=='HeapAlloc':
    k=('h',p[1],p[3]);live[(p[4],p[5])]=(int(p[6],16),k,int(p[1]));cur=k
  elif ev=='HeapRealloc':
    live.pop((p[4],p[6]),None);k=('h',p[1],p[3]);live[(p[4],p[5])]=(int(p[7],16),k,int(p[1]));cur=k
  elif ev=='HeapFree':
    live.pop((p[4],p[5]),None)
  elif ev=='VirtualAlloc' and 'COMMIT' in p[5]:
    b,e=int(p[3],16),int(p[4],16);k=('v',p[1],b);vlive[b]=(e-b,k,int(p[1]));cur=k
  elif ev=='VirtualFree':
    b=int(p[3],16);vlive.pop(b,None)
close()
def report(name,d):
  g=collections.defaultdict(lambda:[0,0,[]])
  for size,k,ts in d.values():
    st=stacks.get(k,('<no stack>',));e=g[st];e[0]+=size;e[1]+=1;e[2].append((ts,size))
  tot=sum(v[0] for v in g.values());print(f'== {name}: {sum(v[1] for v in g.values())} outstanding, {tot/1024:.1f} KB')
  for st,(size,n,ts) in sorted(g.items(),key=lambda kv:-kv[1][0])[:12]:
    print(f'{size/1024:10.1f} KB {n:7d} allocs  first/last {min(ts)[0]/1e6:.1f}/{max(ts)[0]/1e6:.1f} s')
    if times and n>=3: print('               at (s, bytes):',', '.join(f'{t/1e6:.1f} {b}' for t,b in sorted(ts)))
    for x in st: print('              ',x)
report('heap',live);report('VirtualAlloc commits',vlive)
