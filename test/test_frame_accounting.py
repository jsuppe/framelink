#!/usr/bin/env python3
"""Accounting negative controls: missing records must not masquerade as success."""
import importlib.util
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('accounting', Path(__file__).resolve().parents[1] / 'tools/frame_accounting.py')
a = importlib.util.module_from_spec(spec)
spec.loader.exec_module(a)


def event(stage, frame=1, generation=1, **extra):
    return dict(session='video', generation=generation, frame_id=frame,
                pts_ns=frame*1_000_000_000, time_ns=frame*1_000_000_000 + (a.STAGES.index(stage) if stage in a.STAGES else 9),
                stage=stage, **extra)


class Accounting(unittest.TestCase):
    def test_complete_and_missing_negative_control(self):
        events = [event(s, f) for f in range(5) for s in a.STAGES]
        self.assertTrue(a.analyze(events, a.STAGES)['accounting_complete'])
        events.remove(event('consumer_acquire', 2))
        r = a.analyze(events, a.STAGES)
        self.assertFalse(r['accounting_complete'])
        self.assertEqual(r['unclassified_frames'], 1)
        self.assertEqual(r['phases']['steady']['unclassified_missing:consumer_acquire'], 1)

    def test_explicit_drop_and_missing_reason_negative_control(self):
        events = [event('sink_receive'), event('drop', reason='ring_full')]
        self.assertTrue(a.analyze(events, a.STAGES)['accounting_complete'])
        events.pop()
        self.assertFalse(a.analyze(events, a.STAGES)['accounting_complete'])

    def test_generation_reset_and_repeated_presentation(self):
        events = [event(s, generation=g) for g in (1, 2) for s in a.STAGES]
        events.append(event('present_submit'))
        r = a.analyze(events, a.STAGES)
        self.assertEqual(r['unique_frames'], 2)
        self.assertEqual(r['repeat_present_submissions'], 1)
        self.assertTrue(r['accounting_complete'])
        self.assertFalse(r['physical_scanout_verified'])

    def test_duplicate_identity_fails_closed(self):
        events = [event(s) for s in a.STAGES]
        events.append(event('submit'))
        self.assertFalse(a.analyze(events, a.STAGES)['accounting_complete'])

    def test_missing_repeated_gpu_completion_fails_closed(self):
        events = [event(s) for s in a.STAGES]
        for e in events:
            if e['stage'] in ('render_submit', 'gpu_complete'):
                e['submission_id'] = 1
        events.append(event('render_submit', submission_id=2))
        r = a.analyze(events, a.STAGES)
        self.assertFalse(r['accounting_complete'])
        self.assertEqual(len(r['gpu_submission_mismatches']), 1)
        events.append(event('gpu_complete', submission_id=2))
        self.assertTrue(a.analyze(events, a.STAGES)['accounting_complete'])

    def test_trace_loss_fails_closed(self):
        events = [event(s) for s in a.STAGES]
        self.assertFalse(a.analyze(events, a.STAGES, metadata={'trace_lost_events': 1})['accounting_complete'])

    def test_pts_identity_conflict_rejected(self):
        events = [event(s) for s in a.STAGES]
        events[0]['pts_ns'] += 1
        with self.assertRaises(ValueError):
            a.analyze(events, a.STAGES)

    def test_truncated_capture_fails_closed(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)/'trace.csv'
            path.write_text('thread,operation,start_ns,duration_ns,channel_id,sequence,fence,slot,pts_ns,result\n'
                            '0,gst.frame_receive,1,0,99,1,0,0,0,0\n'
                            '0,gst.frame_drop.no_consumer,2,0,99,1,0,0,0,0\n')
            events, meta = a.load_csv([path], 'capture1')
        self.assertFalse(a.analyze(events, a.STAGES, metadata=meta)['accounting_complete'])
        self.assertEqual(meta['missing_trace_footers'], 1)

    def test_sequence_reset_fails_closed(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)/'trace.csv'
            path.write_text('thread,operation,start_ns,duration_ns,channel_id,sequence,fence,slot,pts_ns,result\n'
                            '0,ipc.frame_received,1,0,99,2,1,0,0,0\n'
                            '0,ipc.frame_received,2,0,99,1,1,0,100,0\n')
            with self.assertRaises(ValueError):
                a.load_csv([path], 'capture1')

    def test_legacy_replacement_attributes_previous_not_new_frame(self):
        header = 'thread,operation,start_ns,duration_ns,channel_id,sequence,fence,slot,pts_ns,result\n'
        body = ('0,ipc.frame_received,1,0,99,1,1,0,0,0\n'
                '0,ipc.frame_received,2,0,99,2,2,1,100,1\n'
                '0,flAcquireFrame,3,0,99,2,2,1,100,0\n')
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)/'trace.csv'
            path.write_text(header+body+'# dropped_events=0,rejected_threads=0,capacity_per_thread=10\n')
            events, meta = a.load_csv([path], 'capture1')
        dropped = [e for e in events if e['stage'] == 'drop']
        self.assertEqual(len(dropped), 1)
        self.assertEqual(dropped[0]['pts_ns'], 0)
        self.assertEqual(dropped[0]['reason'], 'pending_replaced')
        r = a.analyze(events, ['ipc_receive','consumer_acquire'], metadata=meta)
        self.assertTrue(r['accounting_complete'])


if __name__ == '__main__':
    unittest.main()
