#!/usr/bin/env python3
"""Run the native relay qualification graphs with tracing on in every process
and check each run with check_relay_trace.py.

Each graph is two hops with PCM (source -> relay -> final reader, three
processes) from the shared relay test binary. Allocation identity is checked
inside the graphs themselves (DMA-BUF inode or D3D11 handle comparison against
the original allocations); the traces show that the relay moved ownership
only, with no payload operation or CPU map.

Requires a clean, exact revision and a new evidence directory. Writes one
directory per run (traces, output, check summary) and summary.json.
"""
import argparse, hashlib, json, os, platform, shutil, subprocess, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import check_relay_trace

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
# name, mode, frames forwarded in total, relay processes, what it is
GRAPHS = [
    ('far5_av', '--relay-three-local-av', 8, 1,
     'FAR4 source-time input and FAR5 authenticated pool transfer (no inherited native handles), PCM'),
    ('broker_process', '--relay-process-diagnostic', 16, 1,
     'broker-composed rings, raw producer; an empty graph (no relay traffic) then 16 frames, PCM'),
    ('broker_reservation', '--relay-reservation-diagnostic', 16, 1,
     'broker-composed rings, GstMemory reservations in the source, PCM'),
    ('decoder', '--relay-decoder-diagnostic', 16, 1,
     'hardware H.264 decode into the source reservations, frame hash carried in the PCM'),
]
SOURCES = ['test/fl_gst_native_relay.cpp', 'test/fl_gst_native_relay_platform.h', 'test/fl_gst_native_relay_process.inc',
           'test/fl_gst_native_relay_av.inc', 'test/fl_gst_native_bin_relay.inc', 'samples/session_native_relay.h',
           'samples/session_pcm_relay.h', 'gst/fl_gst_dmabuf_producer.hpp', 'tools/check_relay_trace.py',
           'tools/run_relay_qualification.py']


def git(*args):
    return subprocess.run(['git', '-C', ROOT, *args], capture_output=True, text=True, check=True).stdout.strip()


def command(*args):
    try:
        return subprocess.run(args, capture_output=True, text=True, timeout=30).stdout.strip()
    except (OSError, subprocess.SubprocessError):
        return None


def main():
    a = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    a.add_argument('--build', required=True, help='build directory with fl_gst_native_relay and the plugin')
    a.add_argument('--out', required=True, help='new evidence directory (must not exist)')
    a.add_argument('--repeat', type=int, default=5)
    a.add_argument('--graphs', nargs='*', default=[g[0] for g in GRAPHS])
    a.add_argument('--allow-dirty', action='store_true', help='development only; evidence must use a clean tree')
    args = a.parse_args()
    status = git('status', '--porcelain', '--untracked-files=no')
    if status and not args.allow_dirty:
        sys.exit('refusing: tracked files differ from the revision\n' + status)
    if os.path.exists(args.out):
        sys.exit(f'refusing: {args.out} exists')
    windows = os.name == 'nt'
    binary = os.path.join(args.build, 'fl_gst_native_relay' + ('.exe' if windows else ''))
    plugin = os.path.join(args.build, 'gstframelink.dll' if windows else 'libgstframelink.so')
    for path in (binary, plugin):
        if not os.path.exists(path):
            sys.exit(f'missing {path}')
    os.makedirs(args.out)
    env_info = {
        'revision': git('rev-parse', 'HEAD'), 'dirty': bool(status), 'platform': platform.platform(),
        'machine': platform.machine(), 'python': platform.python_version(),
        'gstreamer': command('gst-inspect-1.0', '--version'),
        'gpu': command('nvidia-smi', '--query-gpu=name,driver_version', '--format=csv,noheader'),
        'sources_sha256': {p: hashlib.sha256(open(os.path.join(ROOT, p), 'rb').read()).hexdigest() for p in SOURCES},
        'binary_sha256': hashlib.sha256(open(binary, 'rb').read()).hexdigest(),
        'plugin_sha256': hashlib.sha256(open(plugin, 'rb').read()).hexdigest(),
    }
    json.dump(env_info, open(os.path.join(args.out, 'environment.json'), 'w'), indent=1)
    results, failed = [], 0
    for name, mode, frames, relays, what in GRAPHS:
        if name not in args.graphs:
            continue
        for n in range(args.repeat):
            run = os.path.join(args.out, name, f'run{n + 1}')
            traces = os.path.join(run, 'traces')
            os.makedirs(traces)
            env = dict(os.environ, FRAMELINK_TRACE=os.path.join(traces, 'core'), FRAMELINK_GST_TRACE=os.path.join(traces, 'gst'))
            began = time.monotonic()
            p = subprocess.run([os.path.abspath(binary), os.path.abspath(plugin), mode], cwd=run, env=env,
                               capture_output=True, text=True, timeout=300)
            elapsed = time.monotonic() - began
            open(os.path.join(run, 'stdout.txt'), 'w').write(p.stdout)
            open(os.path.join(run, 'stderr.txt'), 'w').write(p.stderr)
            if p.returncode == 77:
                results.append({'graph': name, 'run': n + 1, 'skipped': True, 'reason': p.stderr.strip()[-200:]})
                print(f'SKIP {name} run{n + 1}: {p.stderr.strip()[-120:]}')
                break
            summary = check_relay_trace.check(traces, frames, relays)
            summary.update({'graph': name, 'run': n + 1, 'mode': mode, 'what': what, 'exit': p.returncode,
                            'seconds': round(elapsed, 3), 'output': p.stdout.strip().splitlines()})
            ok = p.returncode == 0 and summary['pass']
            summary['pass'] = ok
            json.dump(summary, open(os.path.join(run, 'check.json'), 'w'), indent=1)
            results.append(summary)
            failed += not ok
            print(('PASS' if ok else 'FAIL'), name, f'run{n + 1}', f'{elapsed:.2f}s', '' if ok else summary['problems'] or f'exit {p.returncode}')
    total = {'environment': env_info, 'repeat': args.repeat,
             'runs': [{k: r.get(k) for k in ('graph', 'run', 'pass', 'skipped', 'exit', 'seconds', 'forwarded', 'final_reads', 'problems')} for r in results],
             'passed': sum(1 for r in results if r.get('pass')), 'failed': failed,
             'skipped': sum(1 for r in results if r.get('skipped'))}
    json.dump(total, open(os.path.join(args.out, 'summary.json'), 'w'), indent=1)
    print(f"{total['passed']} passed, {failed} failed, {total['skipped']} skipped")
    sys.exit(1 if failed else 0)


if __name__ == '__main__':
    main()
