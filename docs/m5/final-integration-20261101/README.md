# M5 final integration review — 2026-11-01

Runtime source: FrameLink `ec4d546`, renderer `151ecd6` built against that library.
No new Windows throughput run is implied: sustained BGRA/NV12 evidence remains the
parent-accepted four70s captures. This review closes integration/evidence gaps,
not physical-output or M5a qualification.

## Fresh checks

- Final complete Linux CTest suite: **61/61 passed**,222.93s, matching runtime
  source. Full log retained in `linux-suite.log.gz`.

- FrameLink hosted run41306653435: Linux and Windows passed on `ec4d546`.
- Renderer Linux full build passed against the updated library, followed by4/4
  targeted tests: av_schedule, av_clock_contract, headless_pacer, nv12_cpu.
- Caspar fresh source-archive build (MSVC2022) of both A/V apps, plugin and real
  audio-alignment regression passed. The real sink regression passed8.60s.
- Corrected external deployment negatives: Linux4/4, Windows4/4. Each verifies the
  intended diagnostic, nonzero peer exits, bounded completion, no live endpoint
  and all child processes reaped. Logs and pipelines are retained below.

## Windows suite / loader check

The complete fresh Windows suite first passed58/59 (227.19s). The remaining
`m5_preconnect` case reached a harness deployment error: gst-launch could not
load framelinksink, rather than the intended absent-consumer deadline. Native
app cases already loaded the plugin explicitly. The harness now includes the
build directory in DLL search PATH, uses a private registry, explicitly loads
the intended plugin, and verifies its filename with gst-inspect before timing
the readiness check. The original failing suite log is retained; final targeted
reruns are recorded separately rather than relabeling the initial run. The
corrected Windows test passed: both normal formats100/100, both launch orders
100/100, legacy negative controls detected and the real readiness deadline
verified. Thus all59 Windows cases have passing final coverage; the initial
full run remains58/59, not retroactively59/59. The changed harness also passed
its affected Linux rerun (both normal formats/start orders100/100, legacy
negative detected, actual readiness timeout). No runtime source changed after
the complete Linux61/61 and Windows suite.

## Test-quality correction

The historical missing-media fixture failed linking jpegdec to BGRA before opening
its nonexistent file. That proves only early pipeline rejection, not file-open
error handling. The new fixture includes conversion/scaling/rate elements and
checks the actual missing-resource diagnostic. Incompatible-caps now uses an
explicit capsfilter rather than a malformed caps token. Platform executable/plugin
names are selected explicitly, and build artifacts are checked before launching;
a missing binary cannot masquerade as a valid application failure.

The historical raw results are preserved, not relabeled as passing the corrected
requirements. Windows cases use freshly built `ec4d546` sources and this harness,
not the old copied performance binaries. Timing is bounded functional evidence,
not a throughput/latency benchmark.

## Scope / review disposition

Android cross-UID20/20 and repaired OpenSLES evidence are in
`../audio-alignment-20261101/`; source/Pcm PTS are preserved, and the output sink
negative-span defect has a causal regression. The diagnostic Android audio-master
mode remains unqualified, as do physical route-unplug/suspend and optical/acoustic
output timing. ProducerMapped is the Android example's supported default.

`../CAPABILITY_MATRIX.md` and `../ANDROID_DEPLOYMENT.md` supersede stale same-APK
and unreviewed-performance descriptions. They do not claim GPU-only GStreamer
transport, unrestricted Android parity, physical scanout or a latency SLA.
GDP/GIO PCM remains justified by `../TRANSPORT_DECISION.md`; no custom PCM pipe.

Final PR/merge state is recorded in the roadmap, not inferred from these tests.

Raw log/CSV artifacts are gzip-compressed without altering bytes, including
Windows CRLF and gst-inspect formatting. Use `gzip -dc FILE.gz` to inspect.

## Controlled-clock regression correction

Final hosted run41307699862 exposed a test-fixture timing race: all large-phase
controls passed, but the small20us comparison produced1599 versus1600 output
samples in separate wall-clock runs. The previous fixture used a real system
clock and allowed background calibration observations, so exact sample equality
was not deterministic. This is retained in `clock-fixture-ci-failure.log.gz`.

The regression now drives GstTestClock waits explicitly, disables automatic
master observations before its sole injected calibration change, and retains
the actual GstAudioBaseSink resampling implementation. Exact guarded/unguarded
small-correction equality remains required; it additionally requires output to
differ from1600, proving small-drift resampling was not silently bypassed.
Large-phase negative controls remain mandatory. No production audio code changed.

Linux50/50 and Caspar50/50 repetitions passed with identical output across all
runs (including1599 for both small-correction cases). A separate ASan run passed
with leak detection disabled for external GStreamer globals; no leak claim.
Controlled integer epochs yield unguarded-321/-1721/-1920 versus the historical
wall-clock-320/-1720/-1919; the invariant is the invalid negative span, not those
old rounding-dependent exact values. Test-only GStreamer-check linkage added.
