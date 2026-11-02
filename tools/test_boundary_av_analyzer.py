#!/usr/bin/env python3
"""Negative controls for the independent A/V timing acceptance tool."""
import csv
import pathlib
import subprocess
import sys
import tempfile
import unittest

ANALYZER = pathlib.Path(__file__).with_name('check_boundary_av.py')

class AudioTimingAcceptance(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.prefix = pathlib.Path(self.tmp.name) / 'case'
        self.video = []
        self.producer = []
        self.consumer = []
        self.relay = []
        for n in range(3):
            pts = n * 10**9 // 60
            source = 1000000000 + pts
            self.video.append(dict(identity=n, pts_ns=pts, source_marker_ns=source,
                                   arrival_ns=source+1000))
            base = dict(identity=n, pts_ns=pts+37000000, source_marker_ns=source,
                        duration_ns=(n+1)*10**9//60-pts, samples=800)
            # Receiver may run before the producer returns from appsrc enqueue.
            self.producer.append(dict(base, arrival_ns=source+2000))
            self.relay.append(dict(base, arrival_ns=source+500))
            self.consumer.append(dict(base, arrival_ns=source+1000))

    def check(self, expected, hops=1):
        tracks = [('.consumer.csv', self.video), ('.audio-producer.csv', self.producer),
                  ('.audio-consumer.csv', self.consumer), ('.audio-relay.csv', self.relay)]
        for suffix, rows in tracks:
            with open(str(self.prefix)+suffix, 'w', newline='') as f:
                writer = csv.DictWriter(f, fieldnames=rows[0].keys())
                writer.writeheader()
                writer.writerows(rows)
        result = subprocess.run([sys.executable, str(ANALYZER), str(self.prefix),
                                 '--frames', '3', '--hops', str(hops)], capture_output=True)
        self.assertEqual(result.returncode == 0, expected, result.stderr.decode())

    def test_receiver_before_push_return_is_valid(self): self.check(True)
    def test_relay_order_is_valid(self): self.check(True, 2)
    def test_before_source_is_invalid(self):
        self.consumer[0]['arrival_ns'] = self.consumer[0]['source_marker_ns']-1
        self.check(False)
    def test_before_relay_is_invalid(self):
        self.consumer[0]['arrival_ns'] = self.relay[0]['arrival_ns']-1
        self.check(False, 2)
    def test_changed_pts_is_invalid(self):
        self.consumer[0]['pts_ns'] += 1000000
        self.check(False)
    def test_missing_pcm_is_invalid(self):
        self.consumer.pop()
        self.check(False)
    def test_wrong_sample_count_is_invalid(self):
        self.consumer[0]['samples'] -= 1
        self.check(False)
    def test_repeated_identity_is_invalid(self):
        self.consumer[1]['identity'] = 0
        self.check(False)

if __name__ == '__main__': unittest.main()
