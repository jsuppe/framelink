#!/usr/bin/env python3
"""6a on Linux: one finite native A/V capture, FrameLink producer -> vkrender.

The Windows renderer proof's case (renderer-integration-a6a1906/probe.py), on
Linux: framelink_av_producer (videotestsrc + silent audiotestsrc) feeds
vkrender's A/V input (--wayland mode), with renderer, transport and GStreamer
traces on. vkrender presents on the RTX 3090 into a private headless GNOME
Shell (Wayland, virtual monitor) that this script starts and stops (decision
12, B1). Audio goes to the user's own PulseAudio server. Present submission is
the vkQueuePresentKHR return, not scanout.

  capture_linux.py --renderer-build DIR --out NEW_DIR [--format NV12] [--width 1920
                   --height 1080 --fps 60 --seconds 5 --latency-ms 0]
"""
import argparse, json, os, pathlib, shutil, signal, socket, subprocess, sys, tempfile, time

a = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
a.add_argument('--renderer-build', required=True, help='vkrender build dir (Release/vkrenderer, framelink/libgstframelink.so)')
a.add_argument('--out', required=True)
a.add_argument('--format', choices=('NV12', 'BGRA'), default='NV12')
a.add_argument('--width', type=int, default=1920)
a.add_argument('--height', type=int, default=1080)
a.add_argument('--fps', type=int, default=60)
a.add_argument('--seconds', type=int, default=5)
a.add_argument('--latency-ms', type=int, default=0)
a.add_argument('--trace-events', type=int, default=262144)
a.add_argument('--present-mode', choices=('fifo', 'mailbox', 'immediate'), help='VKR_PRESENT_MODE (default: the renderer\'s FIFO)')
a.add_argument('--monitor', default='1920x1080@60', help='headless shell virtual monitor')
args = a.parse_args()

b = pathlib.Path(args.renderer_build).resolve()
renderer, producer, plugin = b / 'Release/vkrenderer', b / 'Release/framelink_av_producer', b / 'framelink/libgstframelink.so'
for p in (renderer, producer, plugin):
    if not p.exists():
        sys.exit(f'missing {p}')
out = pathlib.Path(args.out).resolve()
out.mkdir(parents=True, exist_ok=False)
d = out / f'{args.width}x{args.height}-{args.fps}-{args.format}-av'
d.mkdir()
n = args.fps * args.seconds

runtime = pathlib.Path(tempfile.mkdtemp(prefix='fl6a-xdg-'))
os.chmod(runtime, 0o700)
display = 'fl6a-' + str(os.getpid())
user_runtime = f'/run/user/{os.getuid()}'
env = {k: v for k, v in os.environ.items() if not k.startswith(('VKR_', 'FRAMELINK_', 'GST_DEBUG', 'GST_TRACER'))}
env.update(XDG_RUNTIME_DIR=str(runtime), WAYLAND_DISPLAY=display)
env.pop('DISPLAY', None)
if os.path.exists(f'{user_runtime}/pulse/native'):
    env['PULSE_SERVER'] = f'unix:{user_runtime}/pulse/native'

started = time.time()
shell_log = (out / 'gnome-shell.log').open('w')
shell = subprocess.Popen(['dbus-run-session', '--', 'gnome-shell', '--headless', '--wayland', '--no-x11',
                          '--virtual-monitor', args.monitor, f'--wayland-display={display}'],
                         env=env, stdout=shell_log, stderr=subprocess.STDOUT, start_new_session=True)
row = {'case': d.name, 'seconds': args.seconds, 'latency_ms': args.latency_ms, 'display': f'private headless gnome-shell (Wayland, virtual {args.monitor})', 'present_mode': args.present_mode or 'fifo'}
try:
    for _ in range(100):
        if (runtime / display).exists():
            break
        time.sleep(0.1)
    else:
        raise SystemExit('headless shell did not create its Wayland socket')
    time.sleep(1.0)
    with socket.socket() as s:
        s.bind(('127.0.0.1', 0))
        port = s.getsockname()[1]
    for key in ('VKR_TRACE_EVENTS', 'FRAMELINK_TRACE_EVENTS', 'FRAMELINK_GST_TRACE_EVENTS'):
        env[key] = str(args.trace_events)
    env['VKR_TRACE_EVENTS'] = str(2 * args.trace_events)
    env.update(VKR_TRACE=str(d / 'renderer'), FRAMELINK_TRACE=str(d / 'transport'), FRAMELINK_GST_TRACE=str(d / 'gst'),
               VKR_TRACE_RETIRE='1', FRAMELINK_TRACE_RING='1')
    if args.present_mode:
        env['VKR_PRESENT_MODE'] = args.present_mode
    rc = [str(renderer), '--wayland', f'vkr6a-{os.getpid()}', '--windowed', '--no-panel', '--control-port', str(port),
          '--av-exit-on-end', '--av-session', str(d / 'session'), '--av-plugin', str(plugin), '--av-format', args.format,
          '--av-width', str(args.width), '--av-height', str(args.height), '--av-fps', str(args.fps),
          '--av-audio-sink', 'autoaudiosink', '--av-clock', 'producer', '--av-latency-ms', str(args.latency_ms),
          '--av-timeout-ms', '15000', '--av-media-run-ms', str(args.seconds * 1000)]
    pipeline = d / 'source.pipeline'
    pipeline.write_text(f'videotestsrc num-buffers={n} pattern=ball ! @VIDEO_SINK@ audiotestsrc num-buffers={n} '
                        f'samplesperbuffer={48000 // args.fps} volume=0.0 ! @AUDIO_SINK@')
    pc = [str(producer), str(plugin), '--session', str(d / 'session'), '--format', args.format, '--frames', str(n),
          '--width', str(args.width), '--height', str(args.height), '--fps', str(args.fps), '--input-pipeline', str(pipeline),
          '--timeout-ms', '15000', '--media-run-ms', str(args.seconds * 1000)]
    samples = []
    t0 = time.monotonic()
    with (d / 'producer.log').open('w') as po, (d / 'renderer.log').open('w') as ro:
        r = subprocess.Popen(rc, stdout=ro, stderr=subprocess.STDOUT, env=env)
        time.sleep(0.3)
        p = subprocess.Popen(pc, stdout=po, stderr=subprocess.STDOUT, env=env)
        try:
            while time.monotonic() - t0 < args.seconds + 35 and (p.poll() is None or r.poll() is None):
                try:
                    import urllib.request
                    with urllib.request.urlopen(f'http://127.0.0.1:{port}/api/status', timeout=.2) as x:
                        samples.append({'t': time.monotonic() - t0, 'status': json.load(x)})
                except Exception:
                    pass
                time.sleep(0.05)
        finally:
            for q in (p, r):
                if q.poll() is None:
                    q.terminate()
                    try:
                        q.wait(timeout=5)
                    except subprocess.TimeoutExpired:
                        q.kill()
                        q.wait()
    row.update(wall_s=round(time.monotonic() - t0, 2), exit_codes=[p.returncode, r.returncode], commands=[pc, rc])
    (d / 'status.json').write_text(json.dumps(samples, indent=1))
finally:
    try:
        os.killpg(shell.pid, signal.SIGTERM)
        shell.wait(timeout=10)
    except (ProcessLookupError, subprocess.TimeoutExpired):
        try:
            os.killpg(shell.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
    shell_log.close()
    shutil.rmtree(runtime, ignore_errors=True)
(out / 'summary.json').write_text(json.dumps([row], indent=2))
print(json.dumps({k: v for k, v in row.items() if k != 'commands'}))
sys.exit(0 if row.get('exit_codes') == [0, 0] else 1)
