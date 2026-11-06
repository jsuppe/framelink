"""Guard against misleading positional timing joins and hidden missing frames."""
import copy
import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location(
    "timing", Path(__file__).resolve().parents[1] / "tools/m6_raw_timing.py")
timing = importlib.util.module_from_spec(spec)
spec.loader.exec_module(timing)


class TimingJoin(unittest.TestCase):
    def setUp(self):
        self.p = [{"pts_ns": str(x), "push_lateness_ns": "1000000"}
                  for x in (0, 16666666, 33333333)]
        self.c = [{"pts_ns": str(x + 1000000000),
                   "arrival_lateness_ns": str(y)}
                  for x, y in zip((0, 16666666, 33333333),
                                  (2000000, 19000000, 3000000))]

    def test_late_frame_is_retained(self):
        r = timing.analyze(self.p, self.c, 3, 17000000)
        self.assertEqual(r["arrival_misses"], 1)
        self.assertEqual(r["late_frames"][0]["index"], 1)
        self.assertEqual(r["late_frames"][0]["post_push_residual_ns"], 18000000)

    def test_equal_truncated_streams_reject(self):
        with self.assertRaises(ValueError):
            timing.analyze(self.p[:-1], self.c[:-1], 3, 17000000)

    def test_changed_mapping_rejects(self):
        c = copy.deepcopy(self.c)
        c[1]["pts_ns"] = str(int(c[1]["pts_ns"]) + 1)
        with self.assertRaises(ValueError):
            timing.analyze(self.p, c, 3, 17000000)

    def test_duplicate_identity_rejects(self):
        with self.assertRaises(ValueError):
            timing.analyze([self.p[0], self.p[0], self.p[2]],
                           [self.c[0], self.c[0], self.c[2]], 3, 17000000)

    def test_missing_consumer_frame_rejects(self):
        with self.assertRaises(ValueError):
            timing.analyze(self.p, self.c[:-1], 3, 17000000)


if __name__ == "__main__":
    unittest.main()
