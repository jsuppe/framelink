#!/usr/bin/env python3
"""6a: join each source frame's identity from FrameLink's boundaries through
the renderer's GPU completion and presentation submission (decision 12, B1).

One finite native A/V capture (FrameLink A/V producer -> vkrender A/V input),
traced on every side: the producer's GStreamer and transport CSVs and the
renderer's transport and renderer CSVs. Platform-neutral; the Windows proof's
checks (renderer-integration-a6a1906/check_identity.py), generalized:

- every trace file complete: footer dropped_events=0, rejected_threads=0;
- the producer submitted exactly N frames (flEndSubmit) and the renderer
  acquired exactly those N (flAcquireFrame), keyed by (channel, fence, PTS),
  with PTS n*1e9/fps and fence n+1;
- each of av.frame_adopt, av.frame_render_submit, av.frame_gpu_complete and
  av.frame_present_submit (VK_SUCCESS or VK_SUBOPTIMAL_KHR) covers all N, and
  render, completion and present carry the same (frame, submission) pairs;
- causal order per frame: submit start <= acquire end <= adopt <= render
  submit <= GPU complete, and render submit <= present submit;
- no av.frame_drop.* event;
- PCM, separately: the renderer played every block (one per video frame in
  these captures) and every 48 kHz sample, with no late block;
- A/V offset, separately: for each frame's first present submission, the
  submission time minus the audio clock's time for its PTS
  (playback_base_ns + PTS + latency, from the renderer's playback-clock
  record, on the trace clock). Software submission timing, not scanout.

  check_renderer_identity.py CAPTURE_DIR --fps 60 --seconds 5 [--latency-ms 0] [--json OUT]
"""
import argparse, csv, glob, gzip, json, os, sys

PRESENT_ACCEPTED = (0, 1000001003)  # VK_SUCCESS, VK_SUBOPTIMAL_KHR
STAGES = ('av.frame_adopt', 'av.frame_render_submit', 'av.frame_gpu_complete', 'av.frame_present_submit')


class Fail(Exception):
    pass


def need(cond, what):
    if not cond:
        raise Fail(what)


def pct(xs, q):
    xs = sorted(xs)
    return xs[min(len(xs) - 1, int(q * len(xs)))] if xs else None


def analyze(folder, fps, seconds, latency_ms):
    trace, footers = [], {}
    files = sorted(glob.glob(os.path.join(folder, '*.csv')) + glob.glob(os.path.join(folder, '*.csv.gz')))
    need(files, 'no trace CSVs')
    for p in files:
        with (gzip.open(p, 'rt', newline='') if p.endswith('.gz') else open(p, newline='')) as f:
            lines = f.read().splitlines()
        f = [s for s in lines if s.startswith('# dropped_events=')]
        need(len(f) == 1 and f[0].startswith('# dropped_events=0,rejected_threads=0,'), f'trace incomplete: {os.path.basename(p)} {f}')
        footers[os.path.basename(p)] = f[0]
        trace.extend(csv.DictReader(s for s in lines if not s.startswith('#')))

    def select(op):
        ok = PRESENT_ACCEPTED if op == 'av.frame_present_submit' else (0,)
        return [r for r in trace if r['operation'] == op and int(r['result']) in ok]

    def key(r):
        return (r['channel_id'], int(r['fence']), int(r['pts_ns']))

    n = fps * seconds
    send, recv = select('flEndSubmit'), select('flAcquireFrame')
    need(len(send) == n, f'producer submitted {len(send)}, expected {n}')
    need(len(recv) == n, f'renderer acquired {len(recv)}, expected {n}')
    channels = {r['channel_id'] for r in send}
    need(len(channels) == 1, f'{len(channels)} producer channels')
    ch = next(iter(channels))
    expected = {(ch, i + 1, i * 10**9 // fps) for i in range(n)}
    need({key(r) for r in send} == expected, 'producer identities differ from the expected sequence')
    need({key(r) for r in recv} == expected, 'renderer acquired identities differ from those submitted')
    stages = {s: select(s) for s in STAGES}
    for s, rows in stages.items():
        got = {key(r) for r in rows}
        need(got == expected, f'{s}: {len(got & expected)} of {n} identities ({len(got - expected)} unexpected)')
    serials = {s: {(key(r), int(r['duration_ns'])) for r in rows} for s, rows in stages.items() if s != 'av.frame_adopt'}
    need(len({frozenset(x) for x in serials.values()}) == 1, 'render, completion and present carry different submissions')
    send_start = {key(r): int(r['start_ns']) for r in send}
    recv_end = {key(r): int(r['start_ns']) + int(r['duration_ns']) for r in recv}
    adopt = {key(r): int(r['start_ns']) for r in stages['av.frame_adopt']}
    need(all(send_start[k] <= recv_end[k] <= adopt[k] for k in expected), 'submit / acquire / adopt order')
    at = {s: {(key(r), int(r['duration_ns'])): int(r['start_ns']) for r in rows} for s, rows in stages.items() if s != 'av.frame_adopt'}
    need(all(adopt[k[0]] <= at['av.frame_render_submit'][k] <= at['av.frame_gpu_complete'][k] and
             at['av.frame_render_submit'][k] <= at['av.frame_present_submit'][k] for k in at['av.frame_render_submit']),
         'render / completion / present order')
    drops = [r['operation'] for r in trace if r['operation'].startswith('av.frame_drop.')]
    need(not drops, f'{len(drops)} renderer drops: {sorted(set(drops))}')

    with open(os.path.join(folder, 'renderer.log')) as f:
        log = f.read()
    ring = [s for s in log.splitlines() if s.startswith('AV_RING_CONFIG ')]
    need(len(ring) == 1 and f'common_latency_ns={latency_ms * 1000000}' in ring[0], f'ring/latency record {ring}')
    records = [json.loads(s) for s in log.splitlines() if s.startswith('{')]
    complete = [r for r in records if r.get('event') == 'playback-complete']
    blocks, samples = n, seconds * 48000  # the capture's source sends one PCM block per video frame
    need(len(complete) == 1, 'no single playback-complete record')
    c = complete[0]
    need(c['audio_blocks'] == blocks and c['audio_samples'] == samples and c['latency_ns'] == latency_ms * 1000000,
         f'PCM: {c["audio_blocks"]} blocks / {c["audio_samples"]} samples, expected {blocks} / {samples}')
    metrics = [r for r in records if r.get('event') == 'playback-metrics']
    need(len(metrics) == 1 and metrics[0]['late_audio_blocks'] == 0, f'late audio blocks {metrics}')
    clock = [r for r in records if r.get('event') == 'playback-clock']
    need(len(clock) == 1, 'no single playback-clock record')
    base = clock[0]['playback_base_ns'] + clock[0]['latency_ns']
    first_present = {}
    for (k, _), t in at['av.frame_present_submit'].items():
        first_present[k] = min(t, first_present.get(k, t))
    offsets = [(t - (base + k[2])) / 1e6 for k, t in first_present.items()]
    presents = len(stages['av.frame_present_submit'])
    return {
        'pass': True, 'frames': n, 'fps': fps, 'seconds': seconds, 'latency_ms': latency_ms,
        'matched_transport': n,
        'stages': {s: {'unique_frames': len({key(r) for r in rows}), 'events': len(rows)} for s, rows in stages.items()},
        'submissions': len(next(iter(serials.values()))), 'present_events_per_frame': round(presents / n, 3),
        'trace_footers': footers, 'ring': ring[0],
        'pcm': {'audio_blocks': c['audio_blocks'], 'audio_samples': c['audio_samples'], 'late_audio_blocks': metrics[0]['late_audio_blocks'],
                'arrival_drift_ns': metrics[0].get('arrival_drift_ns')},
        'av_offset_ms': {'definition': 'first present submission minus (playback_base_ns + latency + PTS), trace clock',
                         'p50': round(pct(offsets, .5), 3), 'p99': round(pct(offsets, .99), 3),
                         'min': round(min(offsets), 3), 'max': round(max(offsets), 3)},
        'physical_scanout_qualified': False,
        'scope': 'finite scheduled playback: identity through FrameLink, GPU completion and presentation API submission; no scanout or acoustic timing',
    }


def main():
    a = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    a.add_argument('capture')
    a.add_argument('--fps', type=int, required=True)
    a.add_argument('--seconds', type=int, required=True)
    a.add_argument('--latency-ms', type=int, default=0)
    a.add_argument('--json')
    args = a.parse_args()
    try:
        r = analyze(args.capture, args.fps, args.seconds, args.latency_ms)
    except (Fail, OSError, KeyError, ValueError) as e:
        r = {'pass': False, 'error': f'{type(e).__name__}: {e}'}
    if args.json:
        with open(args.json, 'w') as f:
            json.dump(r, f, indent=1)
    print(json.dumps({k: v for k, v in r.items() if k != 'trace_footers'}, indent=1))
    return 0 if r['pass'] else 1


if __name__ == '__main__':
    sys.exit(main())
