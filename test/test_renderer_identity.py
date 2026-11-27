#!/usr/bin/env python3
"""Renderer identity checker (6a) negative controls: a synthetic capture passes,
and each missing or wrong record fails with a named reason."""
import csv
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('identity', Path(__file__).resolve().parents[1] / 'tools/check_renderer_identity.py')
c = importlib.util.module_from_spec(spec)
spec.loader.exec_module(c)

FPS, SECONDS = 2, 3  # 6 frames
FIELDS = ['thread', 'operation', 'start_ns', 'duration_ns', 'channel_id', 'sequence', 'fence', 'slot', 'pts_ns', 'result']
FOOTER = '# dropped_events=0,rejected_threads=0,capacity_per_thread=1024'
BASE = 10_000_000_000


class Capture(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.d = Path(self.tmp.name)
        self.producer, self.renderer = [], []
        for i in range(FPS * SECONDS):
            pts, t = i * 10**9 // FPS, BASE + i * 10**9 // FPS
            row = lambda op, at, dur=0, result=0: dict(thread=1, operation=op, start_ns=at, duration_ns=dur, channel_id='77',
                                                       sequence=i + 1, fence=i + 1, slot=i % 2, pts_ns=pts, result=result)
            self.producer.append(row('flEndSubmit', t - 3_000_000))
            self.renderer.append(row('flAcquireFrame', t - 2_000_000, 100))
            self.renderer.append(row('av.frame_adopt', t - 1_000_000))
            for k, op in enumerate(('av.frame_render_submit', 'av.frame_gpu_complete', 'av.frame_present_submit')):
                self.renderer.append(row(op, t + 1000 * k, i))  # duration_ns carries the submission serial
        self.log = ['AV_RING_CONFIG ring_slots=12 pending_slots=6 common_latency_ns=0',
                    json.dumps({'event': 'playback-clock', 'playback_base_ns': BASE, 'latency_ns': 0}),
                    json.dumps({'event': 'playback-metrics', 'late_audio_blocks': 0, 'arrival_drift_ns': 5}),
                    json.dumps({'event': 'playback-complete', 'audio_blocks': FPS * SECONDS, 'audio_samples': SECONDS * 48000, 'latency_ns': 0})]
        self.write()

    def tearDown(self):
        self.tmp.cleanup()

    def write(self, footer=FOOTER):
        for name, rows in (('producer.1.csv', self.producer), ('renderer.2.csv', self.renderer)):
            with (self.d / name).open('w', newline='') as f:
                f.write('# schema=2,clock=steady_ns\n')
                w = csv.DictWriter(f, fieldnames=FIELDS)
                w.writeheader()
                w.writerows(rows)
                f.write(footer + '\n')
        (self.d / 'renderer.log').write_text('\n'.join(self.log) + '\n')

    def check(self):
        return c.analyze(str(self.d), FPS, SECONDS, 0)

    def fails(self, pattern):
        with self.assertRaisesRegex(c.Fail, pattern):
            self.check()

    def test_valid(self):
        r = self.check()
        self.assertTrue(r['pass'])
        self.assertEqual(r['submissions'], 6)
        self.assertAlmostEqual(r['av_offset_ms']['max'], 0.002)

    def test_lost_trace_events(self):
        self.write(footer='# dropped_events=4,rejected_threads=0,capacity_per_thread=1024')
        self.fails('trace incomplete')

    def test_missing_present(self):
        self.renderer = [r for r in self.renderer if not (r['operation'] == 'av.frame_present_submit' and r['sequence'] == 3)]
        self.write()
        self.fails('av.frame_present_submit: 5 of 6')

    def test_failed_present_does_not_count(self):
        for r in self.renderer:
            if r['operation'] == 'av.frame_present_submit' and r['sequence'] == 2:
                r['result'] = -4  # VK_ERROR_DEVICE_LOST
        self.write()
        self.fails('av.frame_present_submit: 5 of 6')

    def test_renderer_drop(self):
        self.renderer.append(dict(self.renderer[0], operation='av.frame_drop.late'))
        self.write()
        self.fails('renderer drops')

    def test_mismatched_submission(self):
        for r in self.renderer:
            if r['operation'] == 'av.frame_gpu_complete' and r['sequence'] == 4:
                r['duration_ns'] = 99
        self.write()
        self.fails('different submissions')

    def test_present_before_render(self):
        for r in self.renderer:
            if r['operation'] == 'av.frame_present_submit' and r['sequence'] == 5:
                r['start_ns'] -= 10_000_000
        self.write()
        self.fails('render / completion / present order')

    def test_acquire_before_submit(self):
        for r in self.renderer:
            if r['operation'] == 'flAcquireFrame' and r['sequence'] == 1:
                r['start_ns'] -= 10_000_000
        self.write()
        self.fails('submit / acquire / adopt order')

    def test_wrong_identity(self):
        self.producer[2]['fence'] = 30
        self.write()
        self.fails('producer identities')

    def test_missing_pcm(self):
        self.log[3] = json.dumps({'event': 'playback-complete', 'audio_blocks': 5, 'audio_samples': SECONDS * 48000, 'latency_ns': 0})
        self.write()
        self.fails('PCM')

    def test_offset_measured_against_audio_clock(self):
        self.log[1] = json.dumps({'event': 'playback-clock', 'playback_base_ns': BASE - 30_000_000, 'latency_ns': 0})
        self.write()
        self.assertAlmostEqual(self.check()['av_offset_ms']['p50'], 30.002)


if __name__ == '__main__':
    unittest.main()
