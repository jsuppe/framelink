#!/usr/bin/env python3
"""Run native relay rate cells with the paced FAR4 -> FAR5 harness
(fl_gst_local_native_bench_{win,linux}) and analyze each capture with
check_local_native_bench.py.

Per cell, in the rate plan's order: a five-second trace-off preflight, a
matched five-second trace-on identity/copy run, then one long trace-off run
(default 1800 s) only if both earlier gates passed. Every capture and its
analysis are kept, failures included; no repeat replaces a failure.

Requires a clean, exact revision and a new evidence directory.

With --adb DEVICE_DIR the harness is an Android build (fl_gst_local_native_bench_android,
plugin linked statically): each capture runs on the connected device and is
pulled back before the same analysis. Android cells are RGBA (the native
AHardwareBuffer layout; BGRA is stored as RGBA and swizzled by CPU maps) and
NV12 at 1080p. Set ANDROID_SERIAL to pick a device.
"""
import argparse, hashlib, json, os, platform, shlex, subprocess, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from check_local_native_bench import analyze

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CELLS = {'bgra1080p60': ('BGRA', 1920, 1080, 60), 'nv12-1080p60': ('NV12', 1920, 1080, 60),
         'bgra4k30': ('BGRA', 3840, 2160, 30), 'nv12-4k30': ('NV12', 3840, 2160, 30),
         'rgba1080p30': ('RGBA', 1920, 1080, 30), 'nv12-1080p30': ('NV12', 1920, 1080, 30),
         'rgba1080p60': ('RGBA', 1920, 1080, 60)}
DESKTOP_CELLS = ['bgra1080p60', 'nv12-1080p60', 'bgra4k30', 'nv12-4k30']
ANDROID_CELLS = ['rgba1080p30', 'nv12-1080p30', 'rgba1080p60', 'nv12-1080p60']


def git(*args):
    return subprocess.run(['git', '-C', ROOT, *args], capture_output=True, text=True, check=True).stdout.strip()


def adb(*args, check=True, timeout=120):
    p = subprocess.run(['adb', *args], capture_output=True, text=True, timeout=timeout)
    if check and p.returncode:
        sys.exit(f"adb {' '.join(args[:2])} failed: {p.stderr.strip()}")
    return p


def capture(exe, plugin, out, cell, seconds, trace, remote=None):
    fmt, w, h, fps = CELLS[cell]
    began = time.monotonic()
    if remote:
        target = f'{remote}/{os.path.basename(out)}'
        args = [f'{remote}/{os.path.basename(exe)}', 'static', '--out', target, '--seconds', str(seconds), '--width', str(w),
                '--height', str(h), '--fps', str(fps), '--format', fmt] + (['--trace'] if trace else [])
        sh = f"timeout {seconds + 180} {' '.join(shlex.quote(a) for a in args)} >/dev/null 2>&1; echo RC=$?"
        r = adb('shell', sh, check=False, timeout=seconds + 240).stdout
        rc = int(r.rsplit('RC=', 1)[1].split()[0]) if 'RC=' in r else -1
        adb('pull', target, os.path.dirname(out) or '.', check=False, timeout=600)
    else:
        args = [exe, plugin, '--out', out, '--seconds', str(seconds), '--width', str(w), '--height', str(h), '--fps', str(fps), '--format', fmt]
        if trace:
            args.append('--trace')
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
    a.add_argument('--plugin', help='plugin library (not used with --adb: linked statically)')
    a.add_argument('--out', required=True, help='new evidence directory (must not exist)')
    a.add_argument('--cells', nargs='*', help='default: the desktop cells, or the Android cells with --adb')
    a.add_argument('--adb', metavar='DEVICE_DIR', help='run on a connected Android device in this new directory')
    a.add_argument('--duration', type=int, default=1800)
    a.add_argument('--allow-dirty', action='store_true', help='development only')
    args = a.parse_args()
    args.cells = args.cells or (ANDROID_CELLS if args.adb else DESKTOP_CELLS)
    if not args.adb and not args.plugin:
        sys.exit('--plugin is required without --adb')
    status = git('status', '--porcelain', '--untracked-files=no')
    if status and not args.allow_dirty:
        sys.exit('refusing: tracked files differ from the revision\n' + status)
    if os.path.exists(args.out):
        sys.exit(f'refusing: {args.out} exists')
    exe = os.path.abspath(args.exe)
    plugin = os.path.abspath(args.plugin) if args.plugin else None
    remote = args.adb.rstrip('/') if args.adb else None
    if remote:
        if adb('get-state', check=False).stdout.strip() != 'device':
            sys.exit('no Android device attached (adb get-state)')
        if adb('shell', f'[ -e {shlex.quote(remote)} ] && echo exists', check=False).stdout.strip():
            sys.exit(f'refusing: {remote} exists on the device')
    os.makedirs(args.out)
    env = {'revision': git('rev-parse', 'HEAD'), 'dirty': bool(status), 'platform': platform.platform(),
           'exe_sha256': hashlib.sha256(open(exe, 'rb').read()).hexdigest(),
           'plugin_sha256': hashlib.sha256(open(plugin, 'rb').read()).hexdigest() if plugin else 'static (linked into the harness)',
           'duration': args.duration, 'cells': args.cells}
    if remote:
        import run_relay_qualification as rq
        env.update(platform='Android ' + rq.prop('ro.build.version.release') + ' (API ' + rq.prop('ro.build.version.sdk') + ')',
                   device=rq.prop('ro.product.model'), build_fingerprint=rq.prop('ro.build.fingerprint'),
                   gpu=rq.android_gpu(), gstreamer=rq.android_gstreamer(os.path.dirname(exe)))
        adb('shell', f'mkdir -p {shlex.quote(remote)}')
        adb('push', exe, remote + '/', timeout=600)
    else:
        for tool in (['nvidia-smi', '--query-gpu=name,driver_version', '--format=csv,noheader'], ['gst-inspect-1.0', '--version']):
            try:
                env[tool[0]] = subprocess.run(tool, capture_output=True, text=True, timeout=30).stdout.strip()
            except OSError:
                env[tool[0]] = None
    json.dump(env, open(os.path.join(args.out, 'environment.json'), 'w'), indent=1)
    summary = []
    for cell in args.cells:
        pre = capture(exe, plugin, os.path.join(args.out, f'{cell}-preflight'), cell, 5, False, remote)
        traced = capture(exe, plugin, os.path.join(args.out, f'{cell}-trace'), cell, 5, True, remote)
        entry = {'cell': cell, 'preflight': pre['pass_all'], 'trace': traced['pass_all']}
        if pre['pass_all'] and traced['pass_all']:
            long = capture(exe, plugin, os.path.join(args.out, f'{cell}-{args.duration}s'), cell, args.duration, False, remote)
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
