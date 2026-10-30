#!/usr/bin/env python3
"""Bounded two-process FrameLink A/V acceptance driver; Python stdlib only.

Requires a real native FrameLink-capable target. Not a CPU-only CI test.
Logs and structured events are retained even on failure. No shell is used.
"""
import argparse
import json
import pathlib
import subprocess
import tempfile
import threading
import time


class Peer:
    def __init__(self, command, directory, label):
        self.command = command
        self.events = []
        self.lines = []
        self.lock = threading.Lock()
        self.log_path = directory / (label + '.log')
        self.process = subprocess.Popen(command, stdout=subprocess.PIPE,
                                        stderr=subprocess.STDOUT, text=True,
                                        encoding='utf-8', errors='replace')
        self.reader = threading.Thread(target=self._read, daemon=True)
        self.reader.start()

    def _read(self):
        with self.log_path.open('w', encoding='utf-8') as log:
            log.write(json.dumps({'command': self.command}) + '\n')
            for line in self.process.stdout:
                log.write(line)
                log.flush()
                try:
                    event = json.loads(line)
                except (ValueError, TypeError):
                    event = None
                with self.lock:
                    self.lines.append(line)
                    if isinstance(event, dict):
                        self.events.append(event)

    def finish(self, deadline):
        self.process.wait(timeout=max(0.01, deadline - time.monotonic()))
        self.reader.join(timeout=2)
        if self.reader.is_alive():
            raise AssertionError('output reader did not finish')
        return self.process.returncode

    def stop(self):
        if self.process.poll() is None:
            self.process.kill()
        self.process.wait(timeout=5)
        self.reader.join(timeout=2)
        self.process.stdout.close()

    def result(self):
        with self.lock:
            results = [event for event in self.events if 'ok' in event]
        if not results:
            raise AssertionError('missing structured result: ' + str(self.log_path))
        return results[-1]


def require_success(peer, role):
    if peer.process.returncode != 0:
        raise AssertionError(f'{role} exit {peer.process.returncode}: {peer.log_path}')
    result = peer.result()
    if result.get('role') != role or result.get('ok') is not True:
        raise AssertionError(f'{role} failed structured result: {result}')
    if result.get('errors') != 0 or result.get('sessions', 0) < 1:
        raise AssertionError(f'{role} verification/session failure: {result}')
    if role == 'consumer':
        if result.get('video_frames', 0) < 1 or result.get('audio_blocks', 0) < 1:
            raise AssertionError('consumer did not verify both tracks')
    return result


def run_pair(args, root, fmt, first):
    label = fmt.lower() + '-' + first + '-first'
    directory = root / label
    directory.mkdir()
    session = directory / 'session'
    common = [args.plugin, '--session', str(session), '--frames', str(args.frames),
              '--timeout-ms', str(int(args.timeout * 1000)), '--format', fmt,
              '--width', '160', '--height', '90', '--fps', '50',
              '--rate', '48000', '--channels', '2']
    commands = {'producer': [args.producer] + common,
                'consumer': [args.consumer] + common}
    peers = {}
    deadline = time.monotonic() + args.timeout + 10
    try:
        peers[first] = Peer(commands[first], directory, first)
        # A deliberate start-order perturbation, not a substitute for readiness.
        time.sleep(0.3)
        second = 'consumer' if first == 'producer' else 'producer'
        peers[second] = Peer(commands[second], directory, second)
        for peer in peers.values():
            peer.finish(deadline)
        results = {role: require_success(peer, role) for role, peer in peers.items()}
        if results['consumer']['video_frames'] != args.frames:
            raise AssertionError('finite frame target was not verified')
        record = {'case': label, 'passed': True, 'results': results}
        (directory / 'result.json').write_text(json.dumps(record, indent=2) + '\n')
        print(json.dumps(record), flush=True)
    finally:
        for peer in peers.values():
            peer.stop()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--producer', required=True)
    parser.add_argument('--consumer', required=True)
    parser.add_argument('--plugin', required=True)
    parser.add_argument('--output', type=pathlib.Path)
    parser.add_argument('--frames', type=int, default=50)
    parser.add_argument('--timeout', type=float, default=20)
    args = parser.parse_args()
    if args.frames < 1 or args.timeout <= 0:
        parser.error('positive frames and timeout required')
    root = args.output or pathlib.Path(tempfile.mkdtemp(prefix='framelink-m3-'))
    root.mkdir(parents=True, exist_ok=True)
    print('evidence: ' + str(root), flush=True)
    for fmt in ('BGRA', 'NV12'):
        for first in ('producer', 'consumer'):
            run_pair(args, root, fmt, first)


if __name__ == '__main__':
    main()
