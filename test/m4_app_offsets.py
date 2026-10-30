#!/usr/bin/env python3
"""Authenticated streaming apps: preserve nonzero audio origin, reject discontinuity.

Hardware integration, not optical/acoustic lip-sync or throughput qualification.
Uses production producer/consumer binaries. Logs retained including failures.
"""
import argparse
import json
import pathlib
import tempfile
import time
from m3_apps import Peer


def run(args, root, offset, fault=False):
    name = ('negative-reset-' if fault else 'offset-') + str(offset)
    directory = root / name
    directory.mkdir()
    common = [args.plugin, '--session', str(directory / 'session'), '--frames', '80',
              '--timeout-ms', '5000', '--fps', '50', '--rate', '48000',
              '--format', 'BGRA']
    producer = [args.producer] + common + ['--audio-offset-ns', str(offset)]
    if fault:
        producer += ['--fault', 'audio-pts-reset']
    consumer = [args.consumer] + common + ['--stream', '--audio-sink', 'fakesink']
    peers = []
    try:
        peers.append(Peer(producer, directory, 'producer'))
        peers.append(Peer(consumer, directory, 'consumer'))
        deadline = time.monotonic() + 18
        for peer in peers:
            peer.finish(deadline)
        sender, receiver = peers
        ready = [e for e in sender.events if e.get('event') == 'session-ready']
        assert ready and ready[0]['audio_offset_ns'] == offset, ready
        timing = [e for e in receiver.events if e.get('event') == 'playback-timing']
        completed = [e for e in receiver.events if e.get('event') == 'playback-complete']
        if fault:
            assert receiver.process.returncode != 0, 'discontinuity incorrectly succeeded'
            assert not timing and not completed, 'invalid timeline reported completed'
            assert any('discontinu' in line for line in receiver.lines), receiver.lines
        else:
            assert all(p.process.returncode == 0 for p in peers), [p.process.returncode for p in peers]
            assert len(timing) == len(completed) == 1, (timing, completed)
            t, c = timing[0], completed[0]
            assert t['audio_first_running_pts_ns'] == offset, t
            assert t['video_first_running_pts_ns'] % 20000000 == 0, t
            assert t['video_first_running_pts_ns'] < t['video_end_ns'], t
            assert t['video_end_ns'] == 1600000000, t
            assert t['audio_end_ns'] - t['video_end_ns'] == offset, t
            assert c['audio_samples'] == 76800 and c['audio_blocks'] == 80, c
            assert c['video_received'] > 0, c
        result = {'case': name, 'passed': True, 'source_audio_offset_ns': offset,
                  'timing': timing, 'completion': completed,
                  'exit_codes': [p.process.returncode for p in peers]}
        (directory / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
        print(json.dumps(result), flush=True)
    finally:
        for peer in peers:
            peer.stop()


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('producer')
    p.add_argument('consumer')
    p.add_argument('plugin')
    p.add_argument('--output')
    args = p.parse_args()
    root = pathlib.Path(args.output or tempfile.mkdtemp(prefix='m4-app-offsets-'))
    root.mkdir(parents=True, exist_ok=True)
    for offset in (37000000, 123456789):
        run(args, root, offset)
    run(args, root, 37000000, True)
    print('PASS: 3 authenticated app timing cases; evidence=' + str(root))


if __name__ == '__main__':
    main()
