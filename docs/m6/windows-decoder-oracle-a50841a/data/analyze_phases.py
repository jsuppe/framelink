"""Report diagnostic source-oracle phases; never a qualification verdict."""
import csv,json,pathlib,statistics,sys
p=pathlib.Path(sys.argv[1]);rows=list(csv.DictReader(p.open()))
assert len(rows)==150 and [int(r['identity']) for r in rows]==list(range(150))
result={'frames':len(rows),'scope':'source oracle only; thread CPU quantized, wall-minus-CPU is not GPU attribution','phases':{},'slowest_oracles':[]}
for phase in ['map','sha','unmap']:
    wall=[];cpu=[]
    for r in rows:
        prefix='oracle_'+phase
        wb,we,cb,ce=[int(r[prefix+k]) for k in ['_wall_begin_ns','_wall_end_ns','_thread_cpu_begin_ns','_thread_cpu_end_ns']]
        assert int(r['oracle_begin_ns'])<=wb<=we<=int(r['oracle_done_ns'])
        assert ce>=cb
        wall.append((we-wb)/1e6);cpu.append((ce-cb)/1e6)
    result['phases'][phase]={'wall_ms_median':statistics.median(wall),'wall_ms_max':max(wall),'wall_ms_sum':sum(wall),'thread_cpu_ms_median':statistics.median(cpu),'thread_cpu_ms_sum':sum(cpu)}
for r in sorted(rows,key=lambda r:int(r['oracle_done_ns'])-int(r['oracle_begin_ns']),reverse=True)[:12]:
    result['slowest_oracles'].append({'identity':int(r['identity']),'total_wall_ms':(int(r['oracle_done_ns'])-int(r['oracle_begin_ns']))/1e6,**{phase:{'wall_ms':(int(r['oracle_'+phase+'_wall_end_ns'])-int(r['oracle_'+phase+'_wall_begin_ns']))/1e6,'thread_cpu_ms':(int(r['oracle_'+phase+'_thread_cpu_end_ns'])-int(r['oracle_'+phase+'_thread_cpu_begin_ns']))/1e6} for phase in ['map','sha','unmap']}})
print(json.dumps(result,indent=2))
