#!/usr/bin/env python3
"""Linux reproducible transport timing/CPU characterization, not presentation timing.
Uses the real native FrameLink + loopback GDP/TCP fixture. This is not the
production authenticated GIO reader path; run m4_app_offsets.py separately.
"""
import argparse
import json
import pathlib
import resource
import statistics
import subprocess
import time


def percentile(values, percent):
    ordered = sorted(values)
    return ordered[min(len(ordered)-1, int((len(ordered)-1)*percent/100))]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('build', type=pathlib.Path)
    parser.add_argument('--output', type=pathlib.Path, required=True)
    parser.add_argument('--repeats', type=int, default=5)
    args = parser.parse_args()
    if not 1 <= args.repeats <= 100:
        parser.error('--repeats must be 1..100')
    args.output.mkdir(parents=True, exist_ok=True)
    results = []
    for mode in ('baseline', 'jitter', 'audio-leads'):
        flags = {'baseline': [], 'jitter': ['--faults'], 'audio-leads': ['--audio-leads']}[mode]
        for iteration in range(args.repeats):
            command = [str(args.build/'fl_m4_transport_timing'), str(args.build/'libgstframelink.so')] + flags
            before = resource.getrusage(resource.RUSAGE_CHILDREN)
            start = time.monotonic_ns()
            run = subprocess.run(command, capture_output=True, text=True, timeout=30)
            elapsed = (time.monotonic_ns()-start)/1e9
            after = resource.getrusage(resource.RUSAGE_CHILDREN)
            (args.output/f'{mode}-{iteration}.log').write_text(run.stdout+run.stderr)
            if run.returncode:
                raise RuntimeError(f'{mode}-{iteration}: exit {run.returncode}')
            records = [json.loads(line) for line in run.stdout.splitlines() if line.startswith('{')]
            frames = [r for r in records if 'frame' in r]
            assert len(frames) == 100
            assert all(r['producer_offset_ns'] == r['consumer_offset_ns'] for r in frames)
            result = {'mode': mode, 'iteration': iteration, 'pairs': len(frames),
                      'wall_seconds_including_setup': elapsed,
                      'cpu_seconds_including_setup_and_pixel_pcm_verification':
                      after.ru_utime+after.ru_stime-before.ru_utime-before.ru_stime,
                      'known_pcm_payload_bytes': len(frames)*3840,
                      'source_timestamp_quantization_ns': 1000,
                      'presentation_measured': False}
            for track in ('audio', 'video'):
                samples = [(r[f'{track}_receive_ns']-r[f'{track}_send_ns'])/1e6 for r in frames]
                assert min(samples) >= 0
                result[track+'_source_mark_to_arrival_ms'] = {
                    'min': min(samples), 'p50': statistics.median(samples),
                    'p95': percentile(samples,95), 'p99': percentile(samples,99), 'max': max(samples)}
            result['fixture_summary'] = records[-1]
            results.append(result)
            print(json.dumps(result), flush=True)
    (args.output/'summary.json').write_text(json.dumps(results, indent=2)+'\n')


if __name__ == '__main__':
    main()
