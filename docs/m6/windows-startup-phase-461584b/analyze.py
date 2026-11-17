#!/usr/bin/env python3
"""Independently account diagnostic media/phase evidence; never claim acceptance."""
import argparse
import collections
import csv
import json
from pathlib import Path
import re
import sys

EXPECTED = set(range(0, 2_000_000_000, 20_000_000))
PHASES = ['enter','connect_begin','connect_end','hello_receive_begin','hello_receive_end',
          'hello_send_begin','hello_send_end','device_create_begin','device_create_end',
          'device_setup_begin','device_setup_end','ring_receive_begin','ring_receive_end',
          'import_begin','import_end','return']

def read_json_rows(path, issues):
    rows = []
    if not path.exists():
        issues.append('missing '+str(path)); return rows
    for n,line in enumerate(path.read_text(errors='replace').splitlines(),1):
        if line.startswith('{'):
            try: rows.append(json.loads(line))
            except ValueError: issues.append(f'{path.name}:{n}: invalid JSON')
    return rows

def media_index(rows, side, track, issues):
    selected = [r for r in rows if r.get('event')=='media-trace' and r.get('track')==track]
    result = {}
    for row in selected:
        try:
            pts = int(row['pts_ns'])
            if pts in result: issues.append(f'{side} {track}: duplicate PTS {pts}')
            if row.get('side') != side: issues.append(f'{side} {track}: unexpected row side')
            for key in ['duration_ns','bytes','monotonic_ns']: int(row[key])
            if not re.fullmatch('[0-9a-f]{64}',row['sha256']): raise ValueError('hash')
            result[pts] = row
        except (ValueError,KeyError,TypeError): issues.append(f'{side} {track}: malformed media row')
    return result

def content(row):
    return tuple(row[k] for k in ['sha256','bytes','duration_ns'])

def media_report(directory, fmt):
    issues=[]
    produced=read_json_rows(directory/(fmt+'-producer.log'),issues)
    consumed=read_json_rows(directory/(fmt+'-consumer.log'),issues)
    sent={t:media_index(produced,'producer',t,issues) for t in ['video','audio']}
    received={t:media_index(consumed,'consumer',t,issues) for t in ['video','audio']}
    for track in ['video','audio']:
        if set(sent[track]) != EXPECTED: issues.append(f'source {track}: expected exact 100-identity PTS set')
        for pts,row in sent[track].items():
            expected_bytes=3840 if track=='audio' else (57600 if fmt=='BGRA' else 21600)
            if row['bytes']!=expected_bytes or row['duration_ns']!=20_000_000:
                issues.append(f'source {track}: invalid bytes/duration at {pts}')
        for pts,row in received[track].items():
            if pts not in sent[track] or content(row)!=content(sent[track][pts]):
                issues.append(f'received {track}: foreign/corrupt identity {pts}')
    if set(received['audio']) != EXPECTED: issues.append('audio delivery not exact 100 identities')
    terminal=[r for r in consumed if r.get('event')=='playback-complete']
    schedule=[r for r in consumed if r.get('event')=='video-schedule-summary']
    if len(terminal)!=1: issues.append('expected one playback-complete record')
    if len(schedule)!=1: issues.append('expected one video-schedule-summary record')
    if terminal and (terminal[0].get('audio_samples')!=96000 or terminal[0].get('audio_blocks')!=100):
        issues.append('terminal PCM count mismatch')
    if terminal and schedule and terminal[0].get('video_received')!=schedule[0].get('scheduled'):
        issues.append('terminal/schedule count mismatch')
    if schedule and schedule[0].get('received')!=len(received['video']):
        issues.append('receiver identities/schedule input count mismatch')
    for stage in ['received','scheduled','handed-off','late']:
        events=[r for r in consumed if r.get('event')=='video-schedule' and r.get('stage')==stage]
        ids=[r['pts_ns'] for r in events]
        if len(ids)!=len(set(ids)): issues.append(f'duplicate scheduling stage {stage}')
        if any(pts not in sent['video'] for pts in ids): issues.append(f'foreign scheduling stage {stage}')
    sv,rv=sent['video'],received['video']
    common=sorted(set(sv)&set(rv))
    gaps=[{'pts_ns':pts,'source_to_receive_ms':(rv[pts]['monotonic_ns']-sv[pts]['monotonic_ns'])/1e6} for pts in common]
    first_zero=next((g for g in gaps if g['pts_ns']==0),None)
    result={'issues':issues,'source_video_count':len(sv),'received_video_count':len(rv),
            'missing_video_pts':sorted(EXPECTED-set(rv)),'source_audio_count':len(sent['audio']),
            'received_audio_count':len(received['audio']),
            'received_audio_bytes':sum(r['bytes'] for r in received['audio'].values()),
            'terminal':terminal,'schedule_summary':schedule,'pts0_source_to_receive':first_zero,
            'first_common_identity_gap':gaps[0] if gaps else None,
            'max_source_to_receive_ms':max((r['source_to_receive_ms'] for r in gaps),default=None),
            'identity_gaps':gaps,'first_source_monotonic_ns':sv.get(0,{}).get('monotonic_ns')}
    result['legacy_script_late_gate'] = bool(terminal and terminal[0].get('video_late',10)<10)
    result['coordinated_control_complete_zero_late'] = bool(set(rv)==EXPECTED and terminal and
        terminal[0].get('video_received')==100 and terminal[0].get('video_late')==0)
    return result

def phases_report(directory):
    issues=[]; warnings=[]; records=[]
    files=sorted(directory.glob('open-phases-*.csv'))
    if not files: issues.append('no phase CSV files (abnormal exit or wrong DLL is not zero startup cost)')
    for path in files:
        groups=collections.defaultdict(list)
        try:
            for raw in csv.DictReader(path.open()):
                row={k:(v if k=='phase' else int(v)) for k,v in raw.items()}
                groups[row['record']].append(row)
        except (ValueError,KeyError,TypeError) as error:
            issues.append(f'{path.name}: malformed phase CSV: {error}');continue
        for index,rows in groups.items():
            ri=[]; names=[r['phase'] for r in rows]
            success=bool(rows[0]['success'])
            if names[0]!='enter' or names[-1]!='return': ri.append('incomplete terminal marks')
            if success and names!=PHASES: ri.append('successful open missing/duplicate/reordered phases')
            if rows[0]['attempts']>rows[0]['capacity']: ri.append('phase record capacity exceeded')
            if not re.fullmatch(r'open-phases-'+str(rows[0]['pid'])+r'\.csv',path.name): ri.append('PID filename mismatch')
            hz=rows[0]['qpc_hz']
            if hz<=0: ri.append('invalid QPC frequency');hz=1
            intervals=[]
            for a,b in zip(rows,rows[1:]):
                if any(a[k]!=b[k] for k in ['pid','tid','qpc_hz','success']): ri.append('record identity changed')
                if b['qpc']<a['qpc'] or b['steady_ns']<a['steady_ns']: ri.append('wall clock regressed')
                cpu=None
                if a['cpu_valid'] and b['cpu_valid']:
                    if b['kernel_100ns']<a['kernel_100ns'] or b['user_100ns']<a['user_100ns']:ri.append('thread CPU clock regressed')
                    cpu=(b['kernel_100ns']+b['user_100ns']-a['kernel_100ns']-a['user_100ns'])/10000
                else: warnings.append(f'{path.name}/{index}: CPU measurement unavailable')
                intervals.append({'from':a['phase'],'to':b['phase'],'wall_ms':1000*(b['qpc']-a['qpc'])/hz,'thread_cpu_ms':cpu})
            gst_ok=all(r.get('gst_ns',0)>0 for r in rows)
            if gst_ok and any(b['gst_ns']<a['gst_ns'] for a,b in zip(rows,rows[1:])): ri.append('GStreamer clock regressed')
            offsets=[r['gst_ns']-r['steady_ns'] for r in rows] if gst_ok else []
            if not gst_ok: warnings.append(f'{path.name}/{index}: no GStreamer timestamp bridge; do not assume raw clock epoch equality')
            item={'file':path.name,'record':index,'pid':rows[0]['pid'],'tid':rows[0]['tid'],'success':success,'issues':ri,
                  'total_wall_ms':1000*(rows[-1]['qpc']-rows[0]['qpc'])/hz,'intervals':intervals,
                  'steady_begin_ns':rows[0]['steady_ns'],'steady_end_ns':rows[-1]['steady_ns'],
                  'gst_begin_ns':rows[0].get('gst_ns') or None,'gst_end_ns':rows[-1].get('gst_ns') or None,
                  'gst_minus_steady_ns_range':[min(offsets),max(offsets)] if offsets else None}
            records.append(item)
            issues.extend(f'{path.name}/{index}: {x}' for x in ri)
    # Existing native/GStreamer recorders append .PID.csv to the configured prefix.
    trace_files=[]
    for prefix in ['native.csv','gst.csv']:
        found=list(directory.glob(prefix+'.*.csv'))
        if not found: issues.append('missing '+prefix+'.PID.csv')
        for path in found:
            text=path.read_text(errors='replace')
            counters=re.search(r'dropped_events=(\d+),rejected_threads=(\d+)',text)
            if not counters or any(int(x) for x in counters.groups()): issues.append(path.name+': missing footer or dropped trace evidence')
            trace_files.append(path.name)
    return {'issues':issues,'warnings':warnings,'records':records,'trace_files':trace_files}

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('results',type=Path);args=p.parse_args()
    report={'scope':'diagnostic evidence only; no timing/resource/API/M6 acceptance claim','arms':{},'issues':[]}
    if (args.results/'status.json').exists():
        report['runner_status']=json.loads((args.results/'status.json').read_text(encoding='utf-8-sig'))
    for arm in ['audio','audio-coordinated']:
        directory=args.results/arm
        item={'formats':{},'phases':phases_report(directory)}
        for fmt in ['BGRA','NV12']:
            item['formats'][fmt]=media_report(directory,fmt)
            report['issues'].extend(f'{arm}/{fmt}: {i}' for i in item['formats'][fmt]['issues'])
            if arm=='audio-coordinated' and not item['formats'][fmt]['coordinated_control_complete_zero_late']:
                report['issues'].append(f'{arm}/{fmt}: coordinated complete/zero-late control not established')
        report['issues'].extend(f'{arm}: {i}' for i in item['phases']['issues'])
        successes=[r for r in item['phases']['records'] if r['success']]
        if len(successes)<len([f for f in item['formats'].values() if f['source_video_count']]):
            report['issues'].append(f'{arm}: fewer successful open records than reached formats')
        for rec in successes:
            if rec['gst_begin_ns'] is None:continue
            times={f:v['first_source_monotonic_ns'] for f,v in item['formats'].items() if v['first_source_monotonic_ns'] is not None}
            if times:
                nearest=min(times,key=lambda f:abs(times[f]-rec['gst_begin_ns']))
                rec['nearest_format_heuristic']=nearest
                rec['open_begin_minus_first_source_ms']=(rec['gst_begin_ns']-times[nearest])/1e6
                # Proximity is explicitly a heuristic, not authenticated process/log identity.
        report['arms'][arm]=item
    report['evidence_complete']=not report['issues']
    (args.results/'analysis.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({'evidence_complete':report['evidence_complete'],'issues':report['issues'],
                      'report':str(args.results/'analysis.json'),'acceptance_claim':False},indent=2))
    return 0 if report['evidence_complete'] else 1

if __name__=='__main__':sys.exit(main())
