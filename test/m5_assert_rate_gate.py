#!/usr/bin/env python3
"""Gate measured acquire event-span rates, never configured FPS or raw counts.

Requires summarize_rate_trace.py output. Defaults target the matched Caspar
fixtures; NV12 60 retains its measured legacy CPU-conversion floor, not a
claim that NV12 60 is qualified. Physical display cadence is not measured.
"""
import argparse,json,pathlib
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('summary',type=pathlib.Path)
a=p.parse_args(); rows=json.loads(a.summary.read_text()); failures=[]
for row in rows:
    name=row['case']; ops=[x for x in row['operations'] if x['operation']=='flAcquireFrame']
    minimum=35.5 if '1080-60-NV12' in name else 57.0 if '1080-60-BGRA' in name else 29.0 if '1080-30-' in name else 48.0
    if len(ops)!=1 or ops[0]['count']<50:
        failures.append(name+': missing or insufficient acquire evidence');continue
    measured=ops[0]['interval_fps']; ok=measured>=minimum
    print(json.dumps({'case':name,'measured_acquire_fps':measured,'minimum_fps':minimum,'pass':ok,'physical_scanout_measured':False}))
    if not ok:failures.append(name+': measured rate below floor')
if not rows:failures.append('no cases')
if failures:raise SystemExit('\n'.join(failures))
