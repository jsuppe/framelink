#!/usr/bin/env python3
"""Sequential signed cross-app qualification. Never clears device logcat.

Install the three debug APKs first; the death fixtures are debug-only.
Only the dev.framelink.cross.* example packages are stopped/started.
"""
import argparse
import json
import pathlib
import re
import subprocess
import time
import uuid

p = argparse.ArgumentParser()
p.add_argument('--serial', required=True)
p.add_argument('--out', type=pathlib.Path, required=True)
p.add_argument('--case', action='append', default=[])
p.add_argument('--diagnostics', action='store_true')
p.add_argument('--diagnostic-level', type=int, default=5)
p.add_argument('--frames', type=int, default=60)
p.add_argument('--native-rgba', action='store_true', help='Use native RGBA AHB/EGL path for the non-NV12 cases')
p.add_argument('--clock-policy', choices=['producer','audio'], default='producer')
p.add_argument('--preroll-delay-ms', type=int, default=0)
a = p.parse_args()
a.out.mkdir(parents=True, exist_ok=True)
B = 'dev.framelink.cross.broker'
C = 'dev.framelink.cross.client'
I = 'dev.framelink.cross.impostor'


def adb(*args):
    return subprocess.run(['adb', '-s', a.serial, *args], check=True,
                          text=True, stdout=subprocess.PIPE,
                          stderr=subprocess.STDOUT, timeout=25).stdout


def cleanup():
    for package in (C, I, B):
        adb('shell', 'am', 'force-stop', package)


def logs(marker):
    text = adb('logcat', '-d', '-v', 'threadtime', '-s', 'FLCross:I', 'FLCrossGate:I', 'FLGst:I')
    return text.split(marker, 1)[-1] if marker in text else ''


def wait(marker, predicate, timeout=24):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        text = logs(marker)
        if predicate(text):
            return text
        time.sleep(.25)
    raise AssertionError('bounded wait expired')


def results(text, name='receiver-result'):
    return [(m[0], json.loads(m[1])) for m in re.findall(
        name + r' lease=([\w-]+) (\{[^\n]+\})', text)]


def assert_success(text, frames):
    records = results(text)
    assert len(records) == 1, records
    lease, result = records[0]
    assert result['ok'] and result['video'] == frames and result['posts'] == frames, result
    assert result['pcm_samples'] == frames * 48000 // 30, result
    sender = re.findall(r'producer-start lease=' + lease + r' uid=(\d+) pid=(\d+)', text)
    assert len(sender) == 1 and int(sender[0][0]) != result['uid'] and int(sender[0][1]) != result['pid'], sender
    producer = [r for key, r in results(text, 'producer-result') if key == lease]
    assert len(producer) == 1 and producer[0]['ok'], producer
    return dict(lease=lease, **result)


cases = ['bgra', 'nv12', 'wrong-signer', 'wrong-key', 'unsupported-format', 'invalid-bounds', 'revoke', 'replace',
         'producer-death', 'producer-restart', 'broker-death', 'broker-restart',
         'consumer-death', 'consumer-restart', 'background', 'foreground-reopen',
         'audio-focus', 'focus-reopen', 'audible', 'audible-nv12']
selected = a.case or cases
assert all(c in cases for c in selected)
summary = []
previous = set()
try:
    for name in selected:
        if name.endswith(('restart', 'reopen')):
            # Keep the surviving broker/producer services; only recreate the UI.
            adb('shell', 'am', 'force-stop', C)
        else:
            cleanup()
        marker = 'case-' + name + '-' + uuid.uuid4().hex
        adb('shell', 'log', '-t', 'FLCrossGate', marker)
        frames = a.frames if name in ('bgra', 'nv12', 'audible', 'audible-nv12') or name.endswith(('restart', 'reopen')) else 180
        package = I if name == 'wrong-signer' else C
        cmd = ['shell', 'am', 'start', '-W', '-n', package + '/dev.framelink.cross.Viewer',
               '--es', 'format', 'YUY2' if name == 'unsupported-format' else 'NV12' if name in ('nv12','audible-nv12') else 'RGBA' if a.native_rgba else 'BGRA',
               '--ei', 'frames', '0' if name == 'invalid-bounds' else str(frames), '--ei', 'fps', '30',
               '--ei', 'preroll-delay-ms', str(a.preroll_delay_ms),
               '--es', 'clock-policy', a.clock_policy,
               '--ez', 'silent', 'false' if name in ('audible', 'audible-nv12', 'audio-focus', 'focus-reopen') else 'true']
        if name == 'wrong-key':
            cmd += ['--ez', 'wrong-key', 'true']
        if a.diagnostics:
            cmd += ['--ez', 'diagnostics', 'true', '--ei', 'diagnostic-level', str(a.diagnostic_level)]
        if name in ('replace', 'revoke'):
            cmd += ['--es', 'mode', name]
        try:
            adb(*cmd)
            if name in ('producer-death', 'broker-death', 'consumer-death', 'background', 'audio-focus'):
                wait(marker, lambda t: any('playback '+x in t for x in ['producer-system-clock','audio-clock','system-clock-fallback']))
                time.sleep(.35)
                if name in ('producer-death', 'broker-death'):
                    receiver = 'KillProducer' if name == 'producer-death' else 'KillBroker'
                    adb('shell', 'am', 'broadcast', '-n', B + '/dev.framelink.cross.' + receiver)
                elif name == 'consumer-death':
                    adb('shell', 'am', 'force-stop', C)
                elif name == 'audio-focus':
                    adb('shell', 'am', 'broadcast', '-n', B + '/dev.framelink.cross.BorrowFocus')
                else:
                    adb('shell', 'input', 'keyevent', 'KEYCODE_HOME')
            terminal = ('permission_rejected=true' if name == 'wrong-signer' else
                        'receiver-stopped' if name in ('unsupported-format', 'invalid-bounds') else
                        'producer-result' if name == 'consumer-death' else 'receiver-result')
            text = wait(marker, lambda t: terminal in t)
            if name == 'wrong-signer':
                assert 'grant lease=' not in text and not results(text)
                result = {'rejected': True}
            elif name in ('unsupported-format', 'invalid-bounds'):
                assert 'reason=IllegalArgumentException' in text and 'grant lease=' not in text
                result = {'rejected': True}
            elif name in ('wrong-key', 'revoke', 'producer-death', 'broker-death', 'consumer-death', 'background', 'audio-focus'):
                assert not any(r['ok'] for _, r in results(text))
                if name != 'consumer-death':
                    assert len(results(text)) == 1
                if name == 'wrong-key':
                    assert results(text)[0][1]['video'] == results(text)[0][1]['pcm_samples'] == 0
                if name == 'producer-death':
                    assert 'reason=producer-death' in text and 'fault=kill-Producer' in text
                if name == 'broker-death':
                    assert 'fault=kill-Broker' in text and 'stop=broker-death' in text
                    text = wait(marker, lambda t: 'producer broker-death' in t)
                if name == 'consumer-death':
                    # Android may destroy the bound-only broker as soon as its
                    # last client dies, before delivering that Binder death.
                    assert any('reason=' + r in text for r in ('consumer-death', 'broker-destroy'))
                    assert not results(text, 'producer-result')[0][1]['ok']
                if name == 'background':
                    assert 'stop=background' in text
                if name == 'audio-focus':
                    assert 'fault=borrow-focus granted=true' in text and 'stop=audio-focus' in text
                if name == 'revoke':
                    assert 'reason=explicit' in text
                result = {'rejected_or_retired': True}
                if name == 'consumer-death':
                    result['retirement_reason'] = re.findall(r'retired lease=[\w-]+ generation=\d+ reason=([\w-]+)', text)
            else:
                text = wait(marker, lambda t: bool(results(t)) and any(key == results(t)[0][0] for key, _ in results(t, 'producer-result')))
                result = assert_success(text, frames)
                assert result['lease'] not in previous
                previous.add(result['lease'])
                if name == 'replace':
                    old = results(text, 'replaced-old-result')
                    assert len(old) == 1 and not old[0][1]['ok'] and old[0][0] != result['lease']
                    assert 'active_owner_reuse_rejected=true' in text and 'reason=replacement' in text
                    generations = re.findall(r'receiver-(?:start|replacement) lease=[\w-]+ generation=(\d+)', text)
                    assert len(generations) == 2 and int(generations[1]) > int(generations[0])
            summary.append({'case': name, 'native_rgba': a.native_rgba and name not in ('nv12','audible-nv12'), 'pass': True, **result})
            print(json.dumps(summary[-1]), flush=True)
        except Exception as e:
            summary.append({'case': name, 'native_rgba': a.native_rgba and name not in ('nv12','audible-nv12'), 'pass': False, 'error': str(e)})
            print(json.dumps(summary[-1]), flush=True)
            raise
        finally:
            final_log = logs(marker)
            (a.out / (name + '.log')).write_text(final_log)
            if a.diagnostics and a.diagnostic_level < 0 and 'diagnostic_file_saved=true' in final_log:
                try:
                    trace = adb('exec-out', 'run-as', package, 'cat', 'cache/last-trace.txt')
                    (a.out / (name + '.trace')).write_text(trace)
                except subprocess.CalledProcessError:
                    pass  # no native recorder after early rejection/process death
            (a.out / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
finally:
    cleanup()
