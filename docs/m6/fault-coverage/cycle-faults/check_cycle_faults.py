#!/usr/bin/env python3
"""Apply the frozen M6.3 lifecycle gate (tools/m6_bench_matrix.py) to cycle-fault summaries."""
import sys, pathlib
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[4] / "tools"))
from m6_bench_matrix import lifecycle_gate
bad_any = False
for path in sys.argv[1:]:
    s = dict(l.rstrip("\n").split("=", 1) for l in open(path) if "=" in l)
    bad = lifecycle_gate(s)
    bad_any |= bool(bad)
    print(f"{pathlib.Path(path).name}: {'PASS' if not bad else 'FAIL ' + '; '.join(bad)} | cycles {s.get('cycles.ok')} | mem {s.get('long.mem.slope.kb_per_cycle')} KB/cycle | handles {s.get('long.handles.slope.per_cycle')} | threads {s.get('long.threads.slope.per_cycle')} | detect {s.get('cycle_fault.detection_ms')}")
sys.exit(1 if bad_any else 0)
