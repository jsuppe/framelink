#!/usr/bin/env python3
"""Audit identity-bearing software frame events; never infer physical scanout.

JSONL schema: session, generation, frame_id, pts_ns, time_ns, stage, [reason].
CSV adapter supports a SINGLE generation per channel per capture. Reconnects
must be split or exported as JSONL with explicit generation. It fails closed
on duplicate stage PTS rather than joining across a reset/repeated timestamp.
"""
import argparse
import collections
import csv
import gzip
import json
from pathlib import Path

STAGES = ('sink_receive', 'buffer_acquire', 'submit', 'ipc_receive',
          'consumer_acquire', 'schedule', 'adopt', 'render_submit', 'gpu_complete', 'present_submit')
OPS = {'gst.frame_receive': 'sink_receive', 'gst.frame_acquired': 'buffer_acquire',
       'flEndSubmit': 'submit', 'ipc.frame_received': 'ipc_receive',
       'flAcquireFrame': 'consumer_acquire', 'av.frame_schedule': 'schedule',
       'av.frame_adopt': 'adopt',
       'av.frame_render_submit': 'render_submit', 'av.frame_gpu_complete': 'gpu_complete',
       'av.frame_present_submit': 'present_submit'}

# Last stage necessarily reached before this terminal loss. Never let a reason
# hide missing earlier evidence. Unknown reasons are intentionally unclassified.
DROP_BOUNDARIES = {
    **dict.fromkeys(('no_caps', 'no_consumer', 'negotiation', 'ring_full', 'channel_lost'), 'sink_receive'),
    **dict.fromkeys(('map_failed', 'invalid_pitch', 'invalid_pts', 'copy_failed', 'submit_failed'), 'buffer_acquire'),
    'pending_replaced': 'ipc_receive',
    **dict.fromkeys(('late', 'invalid', 'generation_end', 'generation_changed'), 'consumer_acquire'),
    'import_failed': 'schedule',
}
SUBMISSION_STAGES = ('render_submit', 'gpu_complete', 'present_submit')


def load_csv(paths, generation):
    events, metadata = [], {'trace_lost_events': 0, 'legacy_sink_records_without_identity': 0}
    pending = {}
    last_received_sequence = {}
    raw = []
    for path in paths:
        opener = gzip.open if str(path).endswith('.gz') else open
        with opener(path, 'rt') as f:
            lines = f.readlines()
        if not any(line.startswith('# dropped_events=') for line in lines):
            metadata['missing_trace_footers'] = metadata.get('missing_trace_footers', 0) + 1
        for line in lines:
            if line.startswith('# dropped_events='):
                values = dict(item.strip().split('=') for item in line[2:].split(','))
                metadata['trace_lost_events'] += int(values['dropped_events']) + int(values['rejected_threads'])
        for row in csv.DictReader(line for line in lines if not line.startswith('#')):
            row['file'] = str(path)
            raw.append(row)
    for row in sorted(raw, key=lambda r: int(r['start_ns'])):
        op, result = row['operation'], int(row['result'])
        if op == 'gst.sinkRender':
            metadata['legacy_sink_records_without_identity'] += 1
        stage = OPS.get(op)
        if op.startswith(('gst.frame_drop.', 'av.frame_drop.')):
            stage = 'drop'
        if stage is None:
            continue
        if stage not in ('ipc_receive', 'drop') and result != 0 and not (stage == 'present_submit' and result == 1000001003):
            continue
        pts, channel = int(row['pts_ns']), row['channel_id']
        # PTS is a join key ONLY within this explicitly single-generation capture.
        event = dict(session=channel, generation=generation, frame_id=str(pts),
                     pts_ns=pts, time_ns=int(row['start_ns']), stage=stage,
                     sequence=int(row['sequence']))
        if op.startswith('av.frame_'):
            if str(row['slot']) != str(generation):
                raise ValueError('renderer generation differs from --generation; split capture by generation')
            if stage in ('render_submit', 'gpu_complete', 'present_submit'):
                event['submission_id'] = int(row['duration_ns'])
        if stage == 'submit':
            event['time_ns'] += int(row['duration_ns'])
        if stage == 'drop':
            event['reason'] = op.split('.frame_drop.', 1)[1]
        if stage == 'ipc_receive':
            sequence = event['sequence']
            if channel in last_received_sequence and sequence <= last_received_sequence[channel]:
                raise ValueError('transport sequence reset/repeated; split capture by generation')
            last_received_sequence[channel] = sequence
            if result == 1:
                previous = pending.get(channel)
                if previous is not None:
                    events.append(dict(previous, stage='drop', reason='pending_replaced',
                                       time_ns=event['time_ns']))
                else:
                    metadata.setdefault('orphan_replacement_events', 0)
                    metadata['orphan_replacement_events'] += 1
            pending[channel] = event
        elif stage == 'consumer_acquire':
            previous = pending.get(channel)
            if previous and previous['frame_id'] == event['frame_id']:
                pending.pop(channel)
        events.append(event)
    # Legacy wrapper counts are not additional sink_receive events. Without
    # identity records we can report anonymous input deficits, not invent IDs.
    known_input = sum(e['stage'] == 'sink_receive' for e in events)
    submitted = sum(e['stage'] == 'submit' for e in events)
    metadata['anonymous_pre_submit_deficit'] = max(0, metadata['legacy_sink_records_without_identity'] - max(known_input, submitted))
    return events, metadata


def analyze(events, required, startup_ns=1_000_000_000, end_ns=1_000_000_000, metadata=None, expected=None):
    frames = collections.defaultdict(list)
    for e in events:
        if e['stage'] not in STAGES + ('drop',):
            raise ValueError('unknown stage: ' + e['stage'])
        # Never merge generations, even when timestamps/sequence reset.
        frames[(str(e['session']), str(e['generation']), str(e['frame_id']))].append(e)
    expected_map = {}
    if expected is not None:
        for item in expected:
            # CSV uses original PTS as frame_id; JSONL may use explicit source IDs.
            key = (str(item['session']), str(item['generation']), str(item.get('frame_id', item['pts_ns'])))
            if key in expected_map:
                raise ValueError('duplicate expected identity: ' + repr(key))
            if not isinstance(item['pts_ns'], int):
                raise ValueError('expected pts_ns must be an integer')
            expected_map[key] = item['pts_ns']
    bounds = {}
    # Expected endpoints anchor phases even if the entire first/last frame is absent.
    for key, pts in expected_map.items():
        lo, hi = bounds.get(key[:2], (pts, pts))
        bounds[key[:2]] = min(lo, pts), max(hi, pts)
    expected_sessions = {key[:2] for key in expected_map}
    for key, rows in frames.items():
        pts_values = {r['pts_ns'] for r in rows}
        if len(pts_values) != 1:
            raise ValueError('identity maps to multiple PTS: ' + repr(key))
        pts = next(iter(pts_values))
        if key[:2] not in expected_sessions:
            lo, hi = bounds.get(key[:2], (pts, pts))
            bounds[key[:2]] = min(lo, pts), max(hi, pts)
    missing_expected = sorted(set(expected_map) - set(frames))
    unexpected = sorted(set(frames) - set(expected_map)) if expected is not None else []
    expected_pts_mismatches = [dict(identity=key, expected_pts_ns=expected_map[key],
                                    observed_pts_ns=rows[0]['pts_ns'])
                               for key, rows in frames.items()
                               if key in expected_map and expected_map[key] != rows[0]['pts_ns']]
    def phase_for(key, pts):
        lo, hi = bounds[key[:2]]
        return 'startup' if pts < lo + startup_ns else 'end' if pts > hi - end_ns else 'steady'
    phases = {p: collections.Counter() for p in ('startup', 'steady', 'end')}
    details, unique = [], collections.Counter()
    repeats = 0
    ambiguities = []
    submission_mismatches = []
    present_mismatches = []
    event_order_errors = []
    intervals = collections.defaultdict(list)
    for key, rows in sorted(frames.items()):
        by_stage = collections.defaultdict(list)
        for row in rows:
            by_stage[row['stage']].append(row)
        for stage, stage_rows in by_stage.items():
            unique[stage] += 1
            if len(stage_rows) > 1:
                if stage == 'present_submit':
                    repeats += len(stage_rows) - 1
                elif stage not in ('drop', 'render_submit', 'gpu_complete'):
                    ambiguities.append({'identity': key, 'stage': stage,
                                        'count': len(stage_rows)})
        tuples = {}
        for stage in SUBMISSION_STAGES:
            records = by_stage.get(stage, [])
            ids = [r.get('submission_id') for r in records]
            tuples[stage] = collections.Counter(ids)
            if records and (None in ids or len(set(ids)) != len(ids)):
                ambiguities.append({'identity': key, 'stage': stage,
                                    'reason': 'missing_or_duplicate_submission_id'})
        submitted = tuples['render_submit']
        if 'gpu_complete' in required and submitted != tuples['gpu_complete']:
            submission_mismatches.append({'identity': key, 'submitted': dict(submitted),
                                          'completed': dict(tuples['gpu_complete'])})
        if 'present_submit' in required and submitted != tuples['present_submit']:
            present_mismatches.append({'identity': key, 'submitted': dict(submitted),
                                      'presented': dict(tuples['present_submit'])})
        # GPU fence observation and present-call are parallel descendants of
        # queue submission, not a serial GPU-complete -> present pipeline.
        # IPC receive may precede flEndSubmit RETURN, so omit that order check.
        for first, second in (('sink_receive','buffer_acquire'), ('buffer_acquire','submit'),
                              ('consumer_acquire','schedule'), ('schedule','adopt'),
                              ('adopt','render_submit')):
            if first in by_stage and second in by_stage:
                if min(r['time_ns'] for r in by_stage[second]) < min(r['time_ns'] for r in by_stage[first]):
                    event_order_errors.append({'identity': key, 'before': first, 'after': second})
        render_times = {r.get('submission_id'): r['time_ns'] for r in by_stage.get('render_submit', [])}
        for stage in ('gpu_complete', 'present_submit'):
            for record in by_stage.get(stage, []):
                sub = record.get('submission_id')
                if sub in render_times and record['time_ns'] < render_times[sub]:
                    event_order_errors.append({'identity': key, 'before': 'render_submit', 'after': stage,
                                               'submission_id': sub})
        pts = rows[0]['pts_ns']
        phase = phase_for(key, pts)
        missing = [s for s in required if s not in by_stage]
        reasons = sorted({r.get('reason', 'unspecified') for r in by_stage.get('drop', [])})
        drop_evidence_missing = []
        drop_contradictions = []
        if reasons:
            for reason in reasons:
                boundary = DROP_BOUNDARIES.get(reason)
                if boundary is None:
                    drop_evidence_missing.append('unknown_drop_reason:' + reason)
                    continue
                stop = STAGES.index(boundary)
                drop_evidence_missing.extend(stage for stage in STAGES[:stop+1]
                                             if stage in required and stage not in by_stage)
                drop_contradictions.extend(stage for stage in STAGES[stop+1:] if stage in by_stage)
            if drop_evidence_missing or drop_contradictions:
                outcome = 'unclassified_drop_evidence:' + '+'.join(reasons)
            else:
                outcome = 'classified_drop:' + '+'.join(reasons)
        elif missing:
            outcome = 'unclassified_missing:' + ','.join(missing)
        else:
            outcome = 'complete'
        phases[phase][outcome] += 1
        # These are observational edges; no fictitious GPU-complete-to-present latency.
        edges = list(zip(STAGES[:STAGES.index('render_submit')+1], STAGES[1:STAGES.index('render_submit')+1]))
        edges += [('render_submit','gpu_complete'), ('render_submit','present_submit')]
        for a, b in edges:
            if a not in required or b not in required:
                continue
            if a in by_stage and b in by_stage:
                delta = min(r['time_ns'] for r in by_stage[b]) - min(r['time_ns'] for r in by_stage[a])
                intervals[a + '->' + b].append(delta)
        timeline = [{k: r[k] for k in ('stage', 'time_ns', 'sequence', 'submission_id', 'reason') if k in r}
                    for r in sorted(rows, key=lambda r: r['time_ns'])]
        details.append(dict(identity=key, pts_ns=pts, phase=phase, outcome=outcome, missing=missing,
                            drop_evidence_missing=sorted(set(drop_evidence_missing)),
                            drop_contradictions=sorted(set(drop_contradictions)), timeline=timeline))
    for key in missing_expected:
        pts = expected_map[key]
        phase = phase_for(key, pts)
        outcome = 'unclassified_absent_expected_frame'
        phases[phase][outcome] += 1
        details.append(dict(identity=key, pts_ns=pts, phase=phase, outcome=outcome,
                            missing=list(required), drop_evidence_missing=[], drop_contradictions=[], timeline=[]))
    timing = {}
    for stage, values in intervals.items():
        values.sort()
        timing[stage] = {'count': len(values), 'min_ns': values[0], 'max_ns': values[-1],
                         'p50_ns': values[len(values)//2], 'p99_ns': values[min(len(values)-1,int(len(values)*.99))]}
    meta = metadata or {}
    unclassified = sum('unclassified' in d['outcome'] for d in details)
    return dict(schema_version=1, unique_frames=len(frames), unique_by_stage=dict(unique),
                repeat_present_submissions=repeats, required_stages=required,
                absent_required_stages=[s for s in required if not unique[s]],
                phases={k: dict(v) for k, v in phases.items()}, unclassified_frames=unclassified,
                ambiguous_identities=ambiguities, gpu_submission_mismatches=submission_mismatches,
                present_submission_mismatches=present_mismatches, event_order_errors=event_order_errors,
                stage_intervals=timing, metadata=meta,
                identity_universe='expected_manifest' if expected is not None else 'observed_only',
                expected_frames=len(expected_map) if expected is not None else None,
                missing_expected_identities=missing_expected, unexpected_identities=unexpected,
                expected_pts_mismatches=expected_pts_mismatches,
                expected_source_validated=expected is not None and not missing_expected and not unexpected and not expected_pts_mismatches,
                accounting_complete=bool(frames) and not missing_expected and not unexpected and not expected_pts_mismatches and not unclassified and not ambiguities and not submission_mismatches and not present_mismatches and not event_order_errors and not meta.get('trace_lost_events', 0) and not meta.get('anonymous_pre_submit_deficit', 0) and not meta.get('missing_trace_footers', 0) and not meta.get('orphan_replacement_events', 0),
                physical_scanout_verified=False, frames=details)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('files', nargs='+', type=Path)
    p.add_argument('--csv', action='store_true')
    p.add_argument('--generation', help='Required for CSV: one generation per channel only')
    p.add_argument('--required', default=','.join(STAGES))
    p.add_argument('--expected-manifest', type=Path, help='Independent JSONL expected source identities and original PTS; never derive from observed trace')
    p.add_argument('--startup-ms', type=float, default=1000)
    p.add_argument('--end-ms', type=float, default=1000)
    p.add_argument('--output', type=Path)
    p.add_argument('--require-complete', action='store_true')
    args = p.parse_args()
    required = args.required.split(',')
    if any(s not in STAGES for s in required):
        p.error('invalid required stage')
    if args.csv:
        if args.generation is None:
            p.error('--csv requires explicit --generation; do not combine reconnects')
        events, metadata = load_csv(args.files, args.generation)
    else:
        events = [json.loads(line) for f in args.files for line in f.read_text().splitlines() if line.strip()]
        metadata = {}
    expected = None
    if args.expected_manifest:
        expected = [json.loads(line) for line in args.expected_manifest.read_text().splitlines() if line.strip()]
    result = analyze(events, required, int(args.startup_ms*1e6), int(args.end_ms*1e6), metadata, expected)
    output = json.dumps(result, indent=2) + '\n'
    if args.output:
        args.output.write_text(output)
    else:
        print(output, end='')
    return int(args.require_complete and not result['accounting_complete'])


if __name__ == '__main__':
    raise SystemExit(main())
