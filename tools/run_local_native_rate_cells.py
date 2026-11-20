#!/usr/bin/env python3
"""Run native relay rate cells with the paced FAR4 -> FAR5 harness
(fl_gst_local_native_bench_{win,linux}) and analyze each capture with
check_local_native_bench.py.

Per cell, in the rate plan's order: a five-second trace-off preflight, a
matched five-second trace-on identity/copy run, then one long trace-off run
(default 1800 s) only if both earlier gates passed. Every capture and its
analysis are kept, failures included; no repeat replaces a failure.

Requires a clean, exact revision and a new evidence directory.
"""
import argparse, hashlib, json, os, platform, subprocess, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from check_local_native_bench import analyze

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CELLS = {'bgra1080p60': ('BGRA', 1920, 1080, 60), 'nv12-1080p60': ('NV12', 1920, 1080, 60),
         'bgra4k30': ('BGRA', 3840, 2160, 30), 'nv12-4k30': ('NV12', 3840, 2160, 30)}


def git(*args):
    return subprocess.run(['git', '-C', ROOT, *args], capture_output=True, text=True, check=True).stdout.strip()


def capture(exe, plugin, out, cell, seconds, trace):
    fmt, w, h, fps = CELLS[cell]
    args = [exe, plugin, '--out', out, '--seconds', str(seconds), '--width', str(w), '--height', str(h), '--fps', str(fps), '--format', fmt]
    if trace:
        args.append('--trace')
    began = time.monotonic()
    rc = subprocess.run(args, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=seconds + 180).returncode
    elapsed = time.monotonic() - began
    try:
        result = analyze(out) if rc == 0 else {'pass_all': False, 'error': f'harness exit {rc}'}
    except (ValueError, KeyError, OSError, TypeError, ZeroDivisionError) as e:
        result = {'pass_all': False, 'error': str(e)}
    result.update(exit=rc, wall_seconds=round(elapsed, 1), cell=cell, seconds=seconds, trace=trace)
    with open(out + '.analysis.json', 'w') as f:
        json.dump(result, f, indent=1)
    t = result.get('timing_ms', {}).get('scheduled_to_final_arrival')
    print(('PASS' if result['pass_all'] else 'FAIL'), cell, f'{seconds}s', 'trace' if trace else 'off',
          f"arrival p99/max {t['p99']:.2f}/{t['max']:.2f} ms" if t else result.get('error', '')[:160], flush=True)
    return result


def main():
    a = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    a.add_argument('--exe', required=True)
    a.add_argument('--plugin', required=True)
    a.add_argument('--out', required=True, help='new evidence directory (must not exist)')
    a.add_argument('--cells', nargs='*', default=list(CELLS))
    a.add_argument('--duration', type=int, default=1800)
    a.add_argument('--allow-dirty', action='store_true', help='development only')
    args = a.parse_args()
    status = git('status', '--porcelain', '--untracked-files=no')
    if status and not args.allow_dirty:
        sys.exit('refusing: tracked files differ from the revision\n' + status)
    if os.path.exists(args.out):
        sys.exit(f'refusing: {args.out} exists')
    os.makedirs(args.out)
    exe, plugin = os.path.abspath(args.exe), os.path.abspath(args.plugin)
    env = {'revision': git('rev-parse', 'HEAD'), 'dirty': bool(status), 'platform': platform.platform(),
           'exe_sha256': hashlib.sha256(open(exe, 'rb').read()).hexdigest(),
           'plugin_sha256': hashlib.sha256(open(plugin, 'rb').read()).hexdigest(), 'duration': args.duration, 'cells': args.cells}
    for tool in (['nvidia-smi', '--query-gpu=name,driver_version', '--format=csv,noheader'], ['gst-inspect-1.0', '--version']):
        try:
            env[tool[0]] = subprocess.run(tool, capture_output=True, text=True, timeout=30).stdout.strip()
        except OSError:
            env[tool[0]] = None
    json.dump(env, open(os.path.join(args.out, 'environment.json'), 'w'), indent=1)
    summary = []
    for cell in args.cells:
        pre = capture(exe, plugin, os.path.join(args.out, f'{cell}-preflight'), cell, 5, False)
        traced = capture(exe, plugin, os.path.join(args.out, f'{cell}-trace'), cell, 5, True)
        entry = {'cell': cell, 'preflight': pre['pass_all'], 'trace': traced['pass_all']}
        if pre['pass_all'] and traced['pass_all']:
            long = capture(exe, plugin, os.path.join(args.out, f'{cell}-{args.duration}s'), cell, args.duration, False)
            entry.update(duration=long['pass_all'], duration_error=long.get('error'),
                         slopes={r: v.get('resource_slopes_per_min') for r, v in long.get('roles', {}).items()},
                         arrival=long.get('timing_ms', {}).get('scheduled_to_final_arrival'))
        else:
            entry.update(duration=None, duration_error='not run: an earlier gate failed')
        summary.append(entry)
        json.dump({'environment': env, 'cells': summary}, open(os.path.join(args.out, 'summary.json'), 'w'), indent=1)
    ok = all(e['preflight'] and e['trace'] and e['duration'] for e in summary)
    print('ALL PASS' if ok else 'NOT ALL PASS')
    sys.exit(0 if ok else 1)


if __name__ == '__main__':
    main()
