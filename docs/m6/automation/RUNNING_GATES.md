# Reusable M6 deterministic gates

Build the current source first. `tools/m6_validation.py` runs20 independently required gates serially and retains registration, commands, binary hashes, runner hash, logs and JSON summary. An existing output directory is rejected so earlier evidence is not overwritten.

```sh
python3 tools/m6_validation.py --build build --platform linux --out evidence/linux-gates
python tools/m6_validation.py --build build --config Release --platform windows --out evidence/windows-gates
python3 tools/m6_validation.py --build build-arm64 --platform android --adb SERIAL --adb-bin /path/to/adb --remote-dir /data/local/tmp/framelink-runtime --out evidence/android-gates
```

Android requires a prepared compatible runtime directory (shared libraries already installed). The runner pushes executables into a unique subdirectory and retains per-test scratch files; it does not delete device evidence. Executable hashes do not fingerprint every loaded shared-library/driver dependency: final integrated qualification must retain those separately. Desktop uses actual CTest JUnit outcomes, not just a zero aggregate exit. Android preserves the registered skip code. Tests missing from registration fail before running anything.

Exit0 means all20 pass. Exit1 means missing/failed/malformed gates. Exit77 means at least one unsupported skip and no failures: incomplete, never qualification. Linux/Android currently skip the unsupported controlled EGL GPU-delay test; Windows runs the D3D11 equivalent. This must not be waived by an aggregate zero CTest result.

Validation: desktop Linux19pass1skip, device Android19pass1skip and Windows20pass. Ten parser controls cover empty/wrong/duplicate identities, failures/errors, skipped/not-run and malformed XML; both hosted jobs execute those controls. The runner's evidence is deterministic functionality, not soak/performance/physical presentation or final milestone acceptance.

## Other reproducible M6 campaigns

- `tools/m6_bench_matrix.py`: desktop/device session cells and lifecycle campaigns, with raw results; use frozen primary zero-added-delay options in LOW_LATENCY_PLAN.md and PRIMARY_RESULTS.md. Legacy baseline defaults are not primary-profile approval.
- `tools/m6_raw_timing.py` and `tools/m6_bench_report.py`: timing/summary analysis. Keep failed and unsupported cells, independent expected counts and frozen resource limits; do not select only clean repeats.
- `tools/check_boundary_trace.py`, `tools/check_boundary_av.py`: exact native transport/GPU and PCM identity/offset accounting. TestGDP/native benchmarks do not substitute for current coordinated-session renderer.
- `docs/m6/cost/*/reproduce.py` and `docs/m6/observability/*/*.py`: captured host-specific diagnostic commands, hashes and scope. Adapt paths/devices explicitly; do not treat historical artifacts as exact-head proof.

M6.7c remains blocked on M6.8/9 integrated revision and final gates. The historical Windows159ms stall, lifecycle resource failures, unsupported POSIX GPU-delay injection and current renderer integration gaps remain open.
