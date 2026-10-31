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
    if stage in a.SUBMISSION_STAGES:
        extra.setdefault('submission_id', 1)
    if stage in ('present_attempt', 'present_submit'):
        extra.setdefault('result', 0)
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
        events.extend(event(s, submission_id=2) for s in a.SUBMISSION_STAGES)
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
        events.append(event('present_attempt', submission_id=2))
        events.append(event('present_submit', submission_id=2))
        self.assertTrue(a.analyze(events, a.STAGES)['accounting_complete'])

    def test_drop_does_not_hide_missing_predecessor(self):
        events = [event(s) for s in a.STAGES[:a.STAGES.index('consumer_acquire')+1]]
        events.append(event('drop', reason='late'))
        self.assertTrue(a.analyze(events, a.STAGES)['accounting_complete'])
        events.remove(event('sink_receive'))
        r = a.analyze(events, a.STAGES)
        self.assertFalse(r['accounting_complete'])
        self.assertIn('sink_receive', r['frames'][0]['drop_evidence_missing'])

    def test_removed_repeated_present_fails(self):
        events = [event(s) for s in a.STAGES]
        events.extend(event(s, submission_id=2) for s in a.SUBMISSION_STAGES)
        self.assertTrue(a.analyze(events, a.STAGES)['accounting_complete'])
        events.remove(event('present_submit', submission_id=2))
        r = a.analyze(events, a.STAGES)
        self.assertFalse(r['accounting_complete'])
        self.assertEqual(len(r['present_result_errors']), 1)

    def test_unknown_or_contradictory_drop_fails(self):
        events = [event(s) for s in a.STAGES]
        events.append(event('drop', reason='ring_full'))
        self.assertFalse(a.analyze(events, a.STAGES)['accounting_complete'])
        self.assertFalse(a.analyze([event('sink_receive'), event('drop', reason='new_reason')], a.STAGES)['accounting_complete'])

    def test_out_of_order_adopt_and_duplicate_present_fail(self):
        events = [event(s) for s in a.STAGES]
        next(r for r in events if r['stage']=='adopt')['time_ns'] = 0
        self.assertFalse(a.analyze(events, a.STAGES)['accounting_complete'])
        events = [event(s) for s in a.STAGES] + [event('present_submit')]
        self.assertFalse(a.analyze(events, a.STAGES)['accounting_complete'])

    def test_manifest_detects_entire_missing_first_middle_last(self):
        events = [event(s, f) for f in range(5) for s in a.STAGES]
        manifest = [dict(session='video', generation=1, frame_id=f, pts_ns=f*1_000_000_000) for f in range(5)]
        self.assertTrue(a.analyze(events, a.STAGES, expected=manifest)['expected_source_validated'])
        for frame, phase in ((0,'startup'), (2,'steady'), (4,'end')):
            r = a.analyze([e for e in events if e['frame_id'] != frame], a.STAGES, expected=manifest)
            self.assertFalse(r['accounting_complete'])
            self.assertEqual(len(r['missing_expected_identities']), 1)
            self.assertEqual(r['phases'][phase]['unclassified_absent_expected_frame'], 1)

    def test_manifest_rejects_extra_duplicate_and_mismatched_pts(self):
        events = [event(s) for s in a.STAGES]
        item = dict(session='video', generation=1, frame_id=1, pts_ns=1_000_000_000)
        with self.assertRaises(ValueError):
            a.analyze(events, a.STAGES, expected=[item,item])
        r = a.analyze(events, a.STAGES, expected=[])
        self.assertFalse(r['accounting_complete'])
        self.assertEqual(len(r['unexpected_identities']), 1)
        r = a.analyze(events, a.STAGES, expected=[dict(item, pts_ns=999)])
        self.assertFalse(r['accounting_complete'])
        self.assertEqual(len(r['expected_pts_mismatches']), 1)

    def test_manifest_arbitrary_pts_and_generation_identity(self):
        manifest, events = [], []
        for generation, pts in ((1,170), (2,170), (2,3927), (2,812219)):
            manifest.append(dict(session='video', generation=generation, pts_ns=pts))
            for stage in a.STAGES:
                e = event(stage, frame=pts, generation=generation)
                e['pts_ns'] = pts
                events.append(e)
        r = a.analyze(events, a.STAGES, expected=manifest)
        self.assertTrue(r['accounting_complete'])
        self.assertEqual(r['expected_frames'], 4)
        self.assertEqual(r['identity_universe'], 'expected_manifest')
        self.assertEqual(a.analyze(events, a.STAGES)['identity_universe'], 'observed_only')

    def test_rejected_only_presentation_is_accounted_not_completed(self):
        events = [event(s) for s in a.STAGES if s != 'present_submit']
        next(e for e in events if e['stage']=='present_attempt')['result'] = -1000001004
        r = a.analyze(events, a.STAGES)
        self.assertTrue(r['accounting_complete'])
        self.assertEqual(r['frames'][0]['outcome'], 'classified_presentation_failure:-1000001004')

    def test_rejected_then_accepted_repeat_completes_frame(self):
        events = [event(s) for s in a.STAGES if s != 'present_submit']
        next(e for e in events if e['stage']=='present_attempt')['result'] = -1000001004
        events.extend(event(s, submission_id=2) for s in a.SUBMISSION_STAGES)
        r = a.analyze(events, a.STAGES)
        self.assertTrue(r['accounting_complete'])
        self.assertEqual(r['frames'][0]['outcome'], 'complete')
        self.assertEqual(r['frames'][0]['rejected_present_attempts'], 1)
        events.remove(event('present_attempt', submission_id=2))
        self.assertFalse(a.analyze(events, a.STAGES)['accounting_complete'])

    def test_suboptimal_accepted_and_contradictory_result_rejected(self):
        events = [event(s) for s in a.STAGES]
        for e in events:
            if e['stage'] in ('present_attempt','present_submit'):
                e['result'] = 1000001003
        self.assertTrue(a.analyze(events, a.STAGES)['accounting_complete'])
        next(e for e in events if e['stage']=='present_attempt')['result'] = -1000001004
        self.assertFalse(a.analyze(events, a.STAGES)['accounting_complete'])

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
