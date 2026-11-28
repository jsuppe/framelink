#!/usr/bin/env python3
"""6a on Android: one finite native A/V capture into vkrender (decision 12, B1).

vkrender's renderer APK, built with the A/V input (-PgstAndroidRoot), runs the
desktop AvInput on slot 0 (PCM to fakesink: received, counted and timed, no
audio output). Its debug-only AvProducerService runs FrameLink's A/V producer
(synthetic video and PCM) in the APK's own ":avproducer" process, since
FrameLink's abstract sockets need the same app UID. Renderer and transport
traces are on; the renderer exits at the session end. The logs and traces are
pulled with run-as into OUT/<case>/ for tools/check_renderer_identity.py.
Present submission is the vkQueuePresentKHR return, not scanout.

  capture_android.py --serial SERIAL --out NEW_DIR [--format NV12|BGRA]
                     [--width 1920 --height 1080 --fps 60 --seconds 5 --latency-ms 0]
"""
import argparse, json, os, pathlib, subprocess, sys, time

PKG = 'dev.vkrender.renderer'
a = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
a.add_argument('--serial', required=True)
a.add_argument('--out', required=True)
a.add_argument('--format', choices=('NV12', 'BGRA'), default='NV12')
a.add_argument('--width', type=int, default=1920)
a.add_argument('--height', type=int, default=1080)
a.add_argument('--fps', type=int, default=60)
a.add_argument('--seconds', type=int, default=5)
a.add_argument('--latency-ms', type=int, default=0)
args = a.parse_args()


def adb(*cmd, check=True, timeout=60):
    r = subprocess.run(['adb', '-s', args.serial, *cmd], capture_output=True, timeout=timeout)
    if check and r.returncode:
        sys.exit(f'adb {" ".join(cmd)}: {r.stderr.decode(errors="replace")}')
    return r.stdout


def sh(cmd, **kw):
    return adb('shell', cmd, **kw).decode(errors='replace')


out = pathlib.Path(args.out).resolve()
out.mkdir(parents=True, exist_ok=False)
case = f'{args.width}x{args.height}-{args.fps}-{args.format}-av'
d = out / case
d.mkdir()
session = 'av6a-' + time.strftime('%H%M%S')
row = {'case': case, 'seconds': args.seconds, 'latency_ms': args.latency_ms, 'session': session,
       'device': sh('getprop ro.product.model').strip(), 'android': sh('getprop ro.build.version.release').strip()}

sh('input keyevent KEYCODE_WAKEUP')
sh(f'am force-stop {PKG}')
sh(f"run-as {PKG} sh -c 'rm -rf files/av6a-*'", check=False)
adb('logcat', '-c')
t0 = time.monotonic()
sh(f'am start -W -n {PKG}/.MainActivity --es avSession {session} --es avFormat {args.format} '
   f'--ei avWidth {args.width} --ei avHeight {args.height} --ei avFps {args.fps} --ei avLatencyMs {args.latency_ms} '
   f'--ei avMediaRunMs {args.seconds * 1000} --ez avExitOnEnd true --ez trace true')
time.sleep(3)
sh(f'am startservice -n {PKG}/.AvProducerService --es session {session} --es format {args.format} '
   f'--ei width {args.width} --ei height {args.height} --ei fps {args.fps} --ei seconds {args.seconds} --ez trace true')
# The renderer finishes its Activity at the session end; wait for its process to go.
deadline = time.monotonic() + args.seconds + 60
time.sleep(args.seconds + 3)
while time.monotonic() < deadline and sh(f'pidof {PKG}', check=False).strip():
    time.sleep(1)
row['renderer_exited'] = not sh(f'pidof {PKG}', check=False).strip()
row['wall_s'] = round(time.monotonic() - t0, 2)
if not row['renderer_exited']:
    sh(f'am force-stop {PKG}')
files = [f for f in sh(f"run-as {PKG} ls files", check=False).split() if f.startswith(session + "-")]
for f in files:
    data = adb('exec-out', f'run-as {PKG} cat files/{f}', timeout=300)
    # The checker's names: renderer.log, producer.log, and the *.csv traces.
    name = f[len(session) + 1:]
    name = {'renderer.log': 'renderer.log', 'producer.log': 'producer.log'}.get(name, name)
    (d / name).write_bytes(data)
(d / 'logcat.txt').write_bytes(adb('logcat', '-d', '-s', 'vkrender:*', 'AndroidRuntime:E', 'DEBUG:*'))
row['files'] = sorted(p.name for p in d.iterdir())
(out / 'summary.json').write_text(json.dumps([row], indent=2))
print(json.dumps(row))
sys.exit(0 if row['renderer_exited'] else 1)
