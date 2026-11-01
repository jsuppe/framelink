# Windows video rate regression investigation — 2026-10-30

**Gate remains open / playback throughput not qualified.** Browser preview is
not scanout. No physical display or acoustic timing claim is made here.

## Controlled conditions

Caspar NVIDIA RTX5090. Process inventory found no renderer, FrameLink or Python
demo running before measurements; no user demo was killed. Cases run sequentially,
not concurrently. No browser preview requested during renderer measurements.
Tracing enabled equally on compared paths; these are short diagnostic windows,
not endurance qualification. Native transport uses CPU-generated patterns,
GPU-sync allocation and CPU pixel verification on receipt, on both revisions.
This is not a zero-copy producer benchmark.

`m5-rate-evidence/summary.json` preserves executable SHA256s, exact commands,
exit codes and raw logs. `interval-summary.json` excludes process startup by
measuring `(N-1)/(last acquisition completion - first completion)`.

## Native video transport baseline

M4 versus M5 builds on same machine, 1920x1080, BGRA and NV12,30 and60 requested:
all8 cases passed unique pixel identity verification, exact150/300 frames, zero
pattern/sequence gaps. M4/M5 acquisition rates:

- BGRA30:29.997 /30.005
- BGRA60:59.993 /59.985
- NV1230:30.002 /29.994
- NV1260:60.006 /60.005

The application's own FPS includes channel startup, hence approximately28/56;
that is not a steady-state throughput loss. This proves native transport retains
these rates in this workload, NOT that AV-renderer scheduling is qualified.

## AV synthetic source, preview disabled

Six cases in `m5-render-rate-v3`. All exited normally, but not all source frames
survived. 1080p30 BGRA/NV12:146 acquired from150;1080p60 BGRA212/300, NV12180/300.
At60 source FPS, sink-render calls300 but successful transport submissions252
BGRA/222 NV12. These are actual losses, not just a browser artifact.
BGRA renderer loop approximately59Hz; NV12 approximately40.6Hz, reflecting an
additional CPU conversion path. No import failures or invalid PTS observed.

## Exact decoded demo, preview disabled

Existing10-second160x90@50 MJPEG/PCM file used unchanged,500 source video buffers.
BGRA:500 sinkRender,135 successfulSubmit,115 successfulAcquire/due decisions.
Observed event-span rates respectively50.79,13.96,11.88/s; renderer loop59.50/s.
NV12:500,135,116 respectively; acquisition11.56/s, renderer loop59.30/s.
The browser is not involved. Its additional150ms refresh loop independently
caps visible preview refresh below6.67Hz.

The first8 video submissions PTS0..140ms arrive in a1.5ms burst; first acquired
sequence is8. The next successful submission is PTS640ms; subsequent groups are
similarly sparse. `framelinksink` drops an input buffer when flAcquireBuffer
fails its5ms wait. Audio startup/producer lead, the eight-buffer ring, retained
GPU frames and bounded pending queue require investigation together. This is a
measured backpressure loss, not proof that GPU compute cannot keep up.

## Harness failures retained on Caspar (not passes)

Initial AV fixture had adjacent caps filters, then omitted receiver dimensions
for1080p negotiation. v3 corrects both. Earlier output directories remain on
Caspar. Initial legacy renderer probe lacked plugin dependency PATH; corrected
with explicit executable/dependency paths and per-case GST_REGISTRY. Only final
successful cases may count as baseline evidence.

## Next gate

Correct decoded-source/ring pacing loss, then repeat exact decoded source and
matched30/60 source rates with identical source format/copy paths. Require unique
frame counts/rates and interval distributions, not exit0 or a handful of previews.
Keep the milestone blocked on this gate until baselines are met or an explicitly
accepted target/limitation is agreed. No such exception has been accepted.

## Renderer binary identity / device

- RTX5090 driver32.0.16.1714.
- M4 renderer SHA256 C857551DFB4C056309E4EAD4CC1D949B0F310B0293775472649837D47BEACF10.
- M5 renderer SHA256 CB7F60477CB331B67A1A3463F6CAF516F48DC430F4CD2BF0E6DA1D082046EDE6.
- No optical presentation timestamps are collected. `due_decisions` and acquisitions
  do not prove a distinct display scanout. Scene iteration measures rendering loop.

## Matched legacy renderer video-only comparison

Each old/new run acquires120 frames and exits normally, offscreen1280x720 scene,
1920x1080 input; no HTTP preview. Rates over acquisition event-completion span:

- BGRA30 M4/M5:30.45 /30.22.
- BGRA60 M4/M5:59.09 /59.47.
- NV1230 M4/M5:30.17 /30.23.
- NV1260 M4/M5:37.52 /37.33.

This short matched test does not show a new legacy video-path throughput regression.
It also establishes that this existing NV12 renderer CPU-conversion path did NOT
reach60fps at1080p in the M4 baseline. We cannot call it a qualified60fps path.
Scene-loop rates include idle startup; acquisition-event spans exclude channel
startup but still include first-frame transient effects. Producer termination
is harness cleanup after the finite renderer exits; only receiver trace records
are used for this comparison (producer tracing cannot flush on force termination).
The first M4 startup includes registry/plugin discovery cost; not counted as FPS.

Raw traces have dropped_events=0 and rejected_threads=0 where gracefully flushed.
These short diagnostics locate bottlenecks; repeated longer steady-state runs and
render-to-present identification are still needed for full release qualification.
