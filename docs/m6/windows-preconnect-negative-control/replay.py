"""Replay retained raw records through the actual helper's assertion block."""
import argparse,ast,json
from pathlib import Path
from types import SimpleNamespace
p=argparse.ArgumentParser();p.add_argument('--helper',required=True);p.add_argument('--original',required=True);a=p.parse_args()
tree=ast.parse(Path(a.helper).read_text())
loop=next(n for n in tree.body if isinstance(n,ast.For) and isinstance(n.target,ast.Name) and n.target.id=='fmt')
start=next(i for i,n in enumerate(loop.body) if isinstance(n,ast.Assign) and isinstance(n.targets[0],ast.Name) and n.targets[0].id=='sent')
code=compile(ast.Module(body=loop.body[start:],type_ignores=[]),a.helper,'exec')
def check(mode,negative,damage=None):
 root=Path(a.original)/mode
 def records(path):
  rows=[json.loads(x) for x in path.read_text().splitlines() if x.startswith('{')]
  if path.name.endswith('consumer.log'):
   media=[r for r in rows if r.get('event')=='media-trace']
   if damage=='pixels':next(r for r in media if r['track']=='video')['sha256']='corrupt'
   if damage=='pcm':rows.remove(next(r for r in media if r['track']=='audio'))
  return rows
 ns=dict(records=records,plog=root/'BGRA-producer.log',clog=root/'BGRA-consumer.log',fmt='BGRA',a=SimpleNamespace(preconnect_negative_control=negative),results=[])
 try:exec(code,ns)
 except AssertionError:return False
 return True
cases=[('original negative rejected by normal gate',check('legacy',False),False),('original negative accepted as injected control',check('legacy',True),True),('healthy data cannot satisfy negative',check('ready',True),False),('healthy data passes normal gate',check('ready',False),True),('negative still rejects corrupt pixels',check('legacy',True,'pixels'),False),('negative still rejects lost PCM',check('legacy',True,'pcm'),False)]
for name,actual,expected in cases:
 assert actual==expected,(name,actual,expected)
 print('PASS',name)
