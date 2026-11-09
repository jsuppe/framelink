import json,re
from pathlib import Path
root=Path(__file__).parent
rows=[tuple(map(int,m)) for m in re.findall(r'^LIFECYCLE,(\d+),(\d+),(\d+)$',(root/'diagnostic.log').read_text(),re.M)]
assert [r[0] for r in rows]==list(range(100)),f'incomplete/duplicate cycles: {len(rows)}'
def slope(points):
 x=[p[0] for p in points];y=[p[1] for p in points];mx=sum(x)/len(x);my=sum(y)/len(y)
 return sum((a-mx)*(b-my) for a,b in zip(x,y))/sum((a-mx)**2 for a in x)
s={'cycles':len(rows),'first':rows[0],'last':rows[-1],'private_kib_per_cycle_all':slope([(a,b/1024)for a,b,c in rows]),'private_range_bytes':[min(b for a,b,c in rows),max(b for a,b,c in rows)],'handle_range':[min(c for a,b,c in rows),max(c for a,b,c in rows)],'handles_per_cycle_all':slope([(a,c)for a,b,c in rows]),'note':'Mixed EOS order and empty-stream modes, diagnostic only. No replacement of original lifecycle acceptance.'}
(root/'summary.json').write_text(json.dumps(s,indent=2)+'\n');print(json.dumps(s,indent=2))
