import csv,sys,os
d=sys.argv[1]
def load(role):
    with open(os.path.join(d,role+'.video.csv')) as f:
        return {int(r['identity']):r for r in csv.DictReader(f) if (r.get('identity') or '').isdigit() and r.get('metadata_flags')}
rel,rcv=load('relay'),load('receiver')
ids=sorted(i for i in rcv if i+1 in rcv and i+1 in rel)
busy=[];late=[]
for i in ids:
    a=int(rcv[i]['arrival_ns']);c=int(rcv[i]['completed_ns']);n=int(rcv[i+1]['arrival_ns']);f=int(rel[i+1]['forwarded_ns'])
    late.append((n-f)/1e6)
    if f < n-500000: busy.append((n-a)/1e6)
busy.sort();late.sort()
q=lambda xs,p:xs[min(len(xs)-1,int(p*len(xs)))] if xs else float('nan')
print(f'{d.split("/")[-1]}: next frame already waiting in {len(busy)} of {len(ids)} frames; receiver loop when busy p50 {q(busy,.5):.2f} p99 {q(busy,.99):.2f} max {max(busy) if busy else 0:.2f} ms; pickup delay p50 {q(late,.5):.2f} p99 {q(late,.99):.2f} max {late[-1]:.2f} ms')
