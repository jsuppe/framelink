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

With --adb DEVICE_DIR the graphs run on a connected Android device instead
(an Android build: the plugin is linked statically into the test binary).
Each run's output and traces are pulled back and checked on the host with the
same checker. Set ANDROID_SERIAL to pick a device.
"""
import argparse, hashlib, json, os, platform, shlex, shutil, subprocess, sys, time
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
           'samples/session_pcm_relay.h', 'gst/fl_gst_dmabuf_producer.hpp', 'gst/fl_gst_ahb_producer.hpp',
           'tools/check_relay_trace.py', 'tools/run_relay_qualification.py']


def git(*args):
    return subprocess.run(['git', '-C', ROOT, *args], capture_output=True, text=True, check=True).stdout.strip()


def command(*args):
    try:
        return subprocess.run(args, capture_output=True, text=True, timeout=30).stdout.strip()
    except (OSError, subprocess.SubprocessError):
        return None


def adb(*args, check=True):
    p = subprocess.run(['adb', *args], capture_output=True, text=True)
    if check and p.returncode:
        sys.exit(f"adb {' '.join(args[:2])} failed: {p.stderr.strip()}")
    return p


def prop(name):
    return adb('shell', 'getprop', name).stdout.strip()


def android_gpu():
    """SoC plus the GLES renderer SurfaceFlinger reports ("GLES: vendor, renderer, version")."""
    soc = ' '.join(x for x in (prop('ro.soc.manufacturer'), prop('ro.soc.model')) if x)
    gles = next((l[5:].strip() for l in adb('shell', 'dumpsys SurfaceFlinger', check=False).stdout.splitlines()
                 if l.startswith('GLES:')), None)
    return f'{soc}; {gles}' if gles else soc


def android_gstreamer(build):
    """Version of the static GStreamer the Android build links, from its CMake cache."""
    try:
        for line in open(os.path.join(build, 'CMakeCache.txt')):
            if line.startswith('FRAMELINK_GST_ANDROID_ROOT:'):
                pc = os.path.join(line.split('=', 1)[1].strip(), 'lib', 'pkgconfig', 'gstreamer-1.0.pc')
                for l in open(pc):
                    if l.startswith('Version:'):
                        return 'GStreamer ' + l.split(':', 1)[1].strip() + ' (static, Android)'
    except OSError:
        pass
    return None


class Local:
    """Runs a graph in a local directory."""
    def __init__(self, binary, plugin):
        self.binary, self.plugin = os.path.abspath(binary), os.path.abspath(plugin)

    def run(self, mode, run, traces):
        env = dict(os.environ, FRAMELINK_TRACE=os.path.join(traces, 'core'), FRAMELINK_GST_TRACE=os.path.join(traces, 'gst'))
        p = subprocess.run([self.binary, self.plugin, mode], cwd=run, env=env, capture_output=True, text=True, timeout=300)
        return p.returncode, p.stdout, p.stderr


class Device:
    """Runs a graph on an Android device in DEVICE_DIR/<graph>/run<n>, then pulls its traces."""
    def __init__(self, binary, remote):
        self.remote = remote.rstrip('/')
        adb('shell', f'rm -rf {shlex.quote(self.remote)} && mkdir -p {shlex.quote(self.remote)}')
        adb('push', binary, self.remote + '/')
        self.binary = self.remote + '/' + os.path.basename(binary)

    def run(self, mode, run, traces):
        name = os.path.relpath(run, os.path.dirname(os.path.dirname(run)))
        d = f'{self.remote}/{name}'
        q = shlex.quote
        sh = (f'mkdir -p {q(d)}/traces && cd {q(d)} && FRAMELINK_TRACE={q(d)}/traces/core FRAMELINK_GST_TRACE={q(d)}/traces/gst '
              f'timeout 300 {q(self.binary)} static {q(mode)} >stdout.txt 2>stderr.txt; echo RC=$?')
        out = adb('shell', sh).stdout
        rc = int(out.rsplit('RC=', 1)[1].split()[0]) if 'RC=' in out else -1
        adb('pull', f'{d}/traces/.', traces)
        text = {}
        for f in ('stdout.txt', 'stderr.txt'):
            text[f] = adb('shell', f'cat {q(d)}/{f}', check=False).stdout
        return rc, text['stdout.txt'], text['stderr.txt']


def main():
    a = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    a.add_argument('--build', required=True, help='build directory with fl_gst_native_relay and the plugin')
    a.add_argument('--out', required=True, help='new evidence directory (must not exist)')
    a.add_argument('--repeat', type=int, default=5)
    a.add_argument('--graphs', nargs='*', default=[g[0] for g in GRAPHS])
    a.add_argument('--allow-dirty', action='store_true', help='development only; evidence must use a clean tree')
    a.add_argument('--adb', metavar='DEVICE_DIR', help='run on a connected Android device in this scratch directory (replaced)')
    args = a.parse_args()
    status = git('status', '--porcelain', '--untracked-files=no')
    if status and not args.allow_dirty:
        sys.exit('refusing: tracked files differ from the revision\n' + status)
    if os.path.exists(args.out):
        sys.exit(f'refusing: {args.out} exists')
    windows = os.name == 'nt' and not args.adb
    binary = os.path.join(args.build, 'fl_gst_native_relay' + ('.exe' if windows else ''))
    plugin = None if args.adb else os.path.join(args.build, 'gstframelink.dll' if windows else 'libgstframelink.so')
    for path in (binary, plugin):
        if path and not os.path.exists(path):
            sys.exit(f'missing {path}')
    sha = lambda path: hashlib.sha256(open(path, 'rb').read()).hexdigest()
    if args.adb:
        if adb('get-state', check=False).stdout.strip() != 'device':
            sys.exit('no Android device attached (adb get-state)')
        host = {'platform': 'Android ' + prop('ro.build.version.release') + ' (API ' + prop('ro.build.version.sdk') + ')',
                'machine': prop('ro.product.cpu.abi'), 'device': prop('ro.product.model'),
                'build_fingerprint': prop('ro.build.fingerprint'),
                'gstreamer': android_gstreamer(args.build),
                'gpu': android_gpu()}
    else:
        host = {'platform': platform.platform(), 'machine': platform.machine(),
                'gstreamer': command('gst-inspect-1.0', '--version'),
                'gpu': command('nvidia-smi', '--query-gpu=name,driver_version', '--format=csv,noheader')}
    os.makedirs(args.out)
    env_info = {
        'revision': git('rev-parse', 'HEAD'), 'dirty': bool(status), **host, 'python': platform.python_version(),
        'sources_sha256': {p: sha(os.path.join(ROOT, p)) for p in SOURCES},
        'binary_sha256': sha(binary),
        'plugin_sha256': sha(plugin) if plugin else 'static (linked into the binary)',
    }
    runner = Device(binary, args.adb) if args.adb else Local(binary, plugin)
    json.dump(env_info, open(os.path.join(args.out, 'environment.json'), 'w'), indent=1)
    results, failed = [], 0
    for name, mode, frames, relays, what in GRAPHS:
        if name not in args.graphs:
            continue
        for n in range(args.repeat):
            run = os.path.join(args.out, name, f'run{n + 1}')
            traces = os.path.join(run, 'traces')
            os.makedirs(traces)
            began = time.monotonic()
            returncode, stdout, stderr = runner.run(mode, run, traces)
            elapsed = time.monotonic() - began
            open(os.path.join(run, 'stdout.txt'), 'w').write(stdout)
            open(os.path.join(run, 'stderr.txt'), 'w').write(stderr)
            if returncode == 77:
                results.append({'graph': name, 'run': n + 1, 'skipped': True, 'reason': stderr.strip()[-200:]})
                print(f'SKIP {name} run{n + 1}: {stderr.strip()[-120:]}')
                break
            summary = check_relay_trace.check(traces, frames, relays)
            summary.update({'graph': name, 'run': n + 1, 'mode': mode, 'what': what, 'exit': returncode,
                            'seconds': round(elapsed, 3), 'output': stdout.strip().splitlines()})
            ok = returncode == 0 and summary['pass']
            summary['pass'] = ok
            json.dump(summary, open(os.path.join(run, 'check.json'), 'w'), indent=1)
            results.append(summary)
            failed += not ok
            print(('PASS' if ok else 'FAIL'), name, f'run{n + 1}', f'{elapsed:.2f}s', '' if ok else summary['problems'] or f'exit {returncode}')
    total = {'environment': env_info, 'repeat': args.repeat,
             'runs': [{k: r.get(k) for k in ('graph', 'run', 'pass', 'skipped', 'exit', 'seconds', 'forwarded', 'final_reads', 'problems')} for r in results],
             'passed': sum(1 for r in results if r.get('pass')), 'failed': failed,
             'skipped': sum(1 for r in results if r.get('skipped'))}
    json.dump(total, open(os.path.join(args.out, 'summary.json'), 'w'), indent=1)
    print(f"{total['passed']} passed, {failed} failed, {total['skipped']} skipped")
    sys.exit(1 if failed else 0)


if __name__ == '__main__':
    main()
