# M5 performance strategy and implementation audit

Review date: 2026-10-31. **M5 performance gate remains open.**

Scope: the Windows FrameLink/GStreamer/native Vulkan A/V path on Caspar,
with Linux regression and deterministic fault tests. These measurements do
not qualify Android, the earlier Raspberry Pi camera workload, higher
resolutions, physical scanout, or acoustic lip sync by extrapolation.

Code reviewed: FrameLink `b551b63` plus accounting hardening `5596f58`
and expected-source manifest support (worker `7e8126b`);
vkrender `07f98cb`. Later evidence must
identify its exact revision and workload. Historical reports below describe
their own revisions, not the current implementation.

## Executive conclusion

There is real progress, but neither functional tests nor an average near 60 fps
establish complete delivery. Several independent defects and policy interactions
were responsible. The evidence does **not** support a blanket conclusion that
FrameLink or the GPU cannot sustain 1080p60. It also does not yet establish that
the complete native A/V application delivers every frame at that rate.

Three distinctions govern the remaining work:

1. **Designed loss:** bounded latest-wins transport can discard frames when a
   consumer cannot keep up. This is a real policy, not proof that the loss is
   necessary under the user's normal workload.
2. **Configuration/contract:** clock, common latency, frame rate, retained-buffer
   budget, and output mode must fit together. A browser JPEG monitor is not a
   native video display. Changing latency may diagnose pressure but must not
   silently change the requested A/V timing contract.
3. **Implementation defects/costs:** queue-drain scheduling, retirement timing,
   startup work, readiness waits, and CPU conversion caused avoidable losses.
   Several now have controlled before/after evidence and fixes.

## Actual path and loss boundaries

```text
source/decode (original PTS and source ordinal)
  -> GStreamer sink receive
  -> transport buffer acquisition [5 ms timeout can drop this input]
  -> pixel copy/upload and ready signal -> submit
  -> receiver IPC [new pending frame can replace an unacquired frame]
  -> consumer acquisition
  -> bounded timed queue [hold / late-drop / adopt]
  -> actual scene input selection
  -> GPU render submission
       -> observed submission fence completion
       -> present submission -> compositor/display [not yet measured here]
```

The standalone CPU-sample consumer has a separate appsink/drain/deadline path.
It must not be confused with the native renderer's external-video path.

Original media time is distinct from wall-clock arrival, GPU completion, and
presentation. A frame's identity must survive those boundaries. At 120 display
draws/second, a 60 fps input will legitimately be drawn repeatedly; those repeats
must not be counted as new video frames. A successful present call does not
prove that the image reached scanout.

On Windows the current GStreamer sink CPU-maps incoming video and uploads it
through the channel's D3D11 device. Native BGRA can then be sampled through
shared GPU resources. The current NV12 consumer additionally reads back,
converts to RGB on the CPU, and uploads a mirror. Thus **GPU-backed FrameLink
does not make the whole decoded pipeline zero-copy**. GPU-memory negotiation
and direct decoder interoperability belong to planned M5a; they must not be
used to excuse an unresolved M5 throughput defect.

## Experiments and what they actually establish

### 1. Transport versus full application

Matched M4/M5 native transport tests at 1080p BGRA/NV12, 30/60 fps preserved
every tested pixel identity. M4/M5 event-span rates were approximately 30/60 fps
with no sequence gaps. This rules out a basic transport throughput regression
in that workload, not a scheduler or display regression.

Matched old/new video-only renderer tests were approximately 59 fps for BGRA60
but only 37 fps for NV1260. NV12's old implementation was therefore not an
established 60 fps baseline. The user's full-rate requirement remains valid;
matching an inadequate baseline is not acceptance.

Evidence: [matched transport and renderer comparison](windows-rate-gate/README.md).

### 2. Browser preview: output-mode limitation

The original browser demo polled JPEGs every 150 ms: a ceiling below 6.67
updates/second before encoding, HTTP and browser costs. It was not a full-rate
video surface. The updated demo uses a native Vulkan window and keeps browser
status/optional preview separate. This explains part of the visible symptom,
but native traces also demonstrated genuine upstream loss.

### 3. Clock startup phase: policy and startup coordination

The first decoded 50 fps experiment had 500 sink inputs but only 135 successful
submissions and about 115 acquisitions. The ring was repeatedly exhausted.

An early interpretation of a zero WASAPI clock while PAUSED was incorrect:
that is normal. The useful later observation was approximately 302 ms of
non-advancement **after PLAYING**, accumulating a roughly 383 ms media phase
lag against the producer. The bounded video storage could not absorb it.

Separate controls distinguished clock source from epoch anchoring. With the
same queue fix, AudioPreferred acquired about 13 fps; ProducerMapped acquired
about 49.6 fps. A system-clock/legacy-anchor diagnostic also recovered the rate.
The implementation now offers an explicit producer-mapped shared clock policy;
it does not rewrite each track's PTS independently. Joint bounded startup
coordination also avoids opening the producer gate before receiver preroll.

Evidence: [clock controls](clock-policy-fix/README.md),
[startup coordination](startup-coordination/README.md).

### 4. Full scheduled queue: refill-order defect

The renderer checked queue capacity before removing the due front frame. A
full queue therefore skipped an acquisition even though it freed space that
iteration; latest-wins then discarded pending sequences periodically.

The fix allows the unused acquisition budget after the due pop, still at most
one nonblocking acquisition attempt per iteration. Same-policy BGRA1080p60
improved from 237 to 286 acquisitions in the short comparison, about 49.1 to
59.2 event-span fps. Queue capacity and latency did not increase.

Evidence: [refill A/B and regression](clock-policy-fix/README.md).

### 5. NV12: conversion, pacing and readiness are separate costs

Unconditional 16 ms sleep **plus** frame processing limited the old headless
loop. Deadline pacing now budgets work inside the interval. Scalar CPU NV12
conversion was optimized with pixel-equivalence tests. This remains a copy/
conversion path, not a GPU-only path.

The timed consumer also used to require pixel readiness merely to acquire
metadata. A zero-timeout acquisition could miss a frame whose GPU upload was
still completing. It now queues metadata under GPU synchronization, then checks
the ready semaphore before CPU mapping/adoption; an unready frame is retained.
This does not waive the GPU fence or permit reading incomplete pixels.

Combined 45-second offscreen results reached about 59.0/59.1 acquired fps for
BGRA/NV12, but still lost frames. They established improved throughput, not
complete delivery and not native-window qualification.

Evidence: [combined Windows validation](combined-windows-20261031/README.md).

### 6. Standalone appsink: drain/deadline coupling defect

The application blocked while waiting for a frame's presentation deadline.
Meanwhile `appsink max-buffers=3 drop=true` silently evicted incoming samples.
A pad probe before that queue still reported all arrivals. Linux reproduced
100 submitted / 100 pad arrivals / only 52 actual application receipts.

The fix continuously drains while independently servicing deadlines. Appsink
remains bounded at three buffers; silent eviction is disabled. A separate
pending list is bounded by negotiated latency plus three frame slots and a
128 MiB payload ceiling. This reserves explicit scheduling storage; it is not
an unbounded queue. It does add bounded retained CPU-sample memory.

Normal cases now account for 100/100 received and scheduled frames. An injected
500 ms stall deliberately produces explicit late/overflow dispositions, with
all 100 identities accounted for. Those dispositions are **not presentation**.
Linux's eight-case matrix passed; final Windows verification is outstanding at
this audit checkpoint.

Evidence: [consumer drain reproduction and fix](consumer-drain/README.md).

### 7. Native GPU retirement: unnecessary resource retention defect

At the unchanged 100 ms shared latency, native-window NV1260 submitted 560/600
frames and acquired 554. Validation disabled/enabled had similar rates, so the
validation layer was not the cause. Changing only latency to 80 ms restored
throughput; that diagnosed storage pressure but was not shipped as a fix.
An early-adoption control helped only partly and was also discarded.

The actual defect was bundling a retired input's consumed signal with the next
scene submission. A buffer no longer needed by earlier draws could remain
unavailable while waiting for a new swapchain image and new input readiness.

The fix signals consumption in a separate submission on the **same GPU queue**,
ordered after earlier reads but before the next swapchain acquisition. It does
not release currently sampled frames early, bypass GPU completion, add a CPU
wait, enlarge the ring, or lower the A/V latency.

Same 100 ms / eight slots / six pending: 560 -> 599 successful submissions and
554 -> 597 acquisitions out of 600 inputs. This is strong causal evidence, not
a zero-loss pass. An event-span rate above 60 in this short run must not be
reported as screen FPS or full clip completeness.

Renderer commits: `76762f2` (retirement), `07f98cb` (capacity diagnostics).
See vkrender `docs/m5/NATIVE_RING_BUDGET.md`.

A later 45-second identity-traced NV1260 old-retirement control lost 45 frames:
eight startup and **37 steady ring-full losses**, versus nine startup and **zero
steady losses** in the fixed capture. Old-control successful unique frames per
successive five-second media interval were 276, 282, 297, then 300 in each
remaining interval; fixed counts were 291 then 300 throughout. The old path
could recover to full rate: this is a phase-sensitive retention/headroom problem,
not an invariant 56 fps hardware ceiling. Retain both captures, not only the
most favorable short comparison.

## Designed loss and configuration contract

`gst/gstframelink.cpp::sinkRender` waits 5 ms for a writable transport buffer,
then drops on timeout/busy and keeps the pipeline running. This is currently a
code policy, not a demonstrated user-tunable lossless mode. Windows transport
can replace a pending unacquired frame with a newer one. Increasing only the
timeout cannot free a buffer whose owner has not retired it.

At 60 fps, 100 ms corresponds to six future frames. Eight slots nominally cover
six pending, one current, and one writer. Extra retained GPU reads, jitter or
source bursts consume additional headroom. The retirement fix removes avoidable
retention; it does not guarantee arbitrary stalls fit. Count simultaneously
owned storage, not just individual queue sizes; buffers shared between stages
must not be double-counted.

The current diagnostic warns above the nominal latency envelope and does not
silently reduce latency. Strict complete-delivery/backpressure behavior versus
latest-wins should be an explicit contract if both are exposed in future. No
finite buffering can promise zero loss, bounded latency and uninterrupted
production under arbitrarily slow consumers simultaneously.

For the present gate, **ordinary supported 1080p60 playback should not incur
avoidable losses**. An overload policy is not a reason to accept steady-state
bugs. Startup loss is also a defect to resolve, not something to hide by choosing
a favorable measurement window.

## What needed improvement in our strategy

- Aggregate FPS located trouble but could hide repeated images and whole-clip
  deficits. Short event-span rates are diagnostic, not an acceptance gate.
- Sink pad arrival was confused with application delivery. Measurement must be
  after each potentially dropping queue, with identity and reason.
- Browser, offscreen and native-window workloads were initially compared too
  loosely. Vsync/retirement changed occupancy even on the same GPU.
- The initial clock A/B changed two variables. Separate source/anchor controls
  were needed before attributing causality; those controls now exist.
- Full-rate transport and passing functional suites did not qualify the full
  A/V renderer. Similarly, validated accounting is not successful delivery.
- Early traces omitted pre-submit identity, preventing retrospective assignment
  of every missing frame. Those gaps are reported as unknown, not guessed.
- The new accounting validator itself needed an adversarial review. It initially
  accepted missing pre-drop stages and an omitted repeated present event. These
  defects are fixed in `5596f58`; 14 negative/unit tests pass and all four fresh
  captures pass reanalysis. Expected-source manifest enforcement now detects
  entirely absent inputs; 17 tests pass including missing first/middle/last
  identities and unexpected/duplicate/conflicting manifests. All four captures
  also pass against independently generated fixture identities. Reports without
  a manifest explicitly say observed-only. A complete-looking report is not
  sufficient without those checks.
- A diagnostic environment variable is not proof an injection ran. The latest
  native negative test exposed that the existing GPU delay gate applies only
  to headless rendering. Native import-failure coverage and actual headless
  GPU-delay coverage must be reported separately unless the gate is extended.

### Newly isolated remaining defects

**Lazy connection after clock start.** Transport scope traces place roughly
158 ms in the first `flOpenProducer`, inside the first sink-render callback;
the ordinary writable-buffer acquisition is not responsible for that delay.
The common clock is already advancing. PTS zero is late, followed by catch-up
pending replacement/ring-full losses. All observed normal-run losses in the
four latest native cases are in startup. Fix `26f495f` now connects/imports
before the final common epoch/GO, without changing original PTS or 100 ms media
latency. A deterministic Linux 350 ms import-delay control loses 12–13 frames
with old lazy setup; the fixed version receives/schedules 100/100 with no late
frames in both formats. Windows native validation is underway. This is bounded
initialization, not arbitrary added playback buffering. Native driver calls
cannot be forcibly interrupted; the deadline is checked on return and retries.

**Native present semaphore reuse.** A fresh native-window Khronos run reports
`VUID-vkQueueSubmit-pSignalSemaphores-00067`: a per-flight binary render-finished
semaphore can still belong to a presentation when reused for a different
swapchain image. This is separate from the consumed timeline-semaphore fix.
Fix `dbf6a45` assigns render-finished semaphores per acquired swapchain image.
Windows native normal/resize tests now report zero validation errors. A genuine
headless 40 ms GPU-delay plus 40 import-failure test also has zero validation
errors and matching completion identities. The resize negative still exposes
an accounting gap: a rejected present attempt was being filtered from analysis.
That attempt must be retained/classified rather than claimed successful or
silently subtracted. Good unique-frame counts do not waive a correctness failure.

The old-retirement native renderer reproduces the same two binary-semaphore
validation errors. This defect predates the separate timeline-retirement change;
it was exposed by extending validation to the actual native-window path.

## Revised execution and acceptance plan

1. Freeze configuration and identity: executable/source hashes, platform, source
   pipeline, format, dimensions/FPS, clocks, common latency, ring/queue sizes,
   output mode, trace mode, display refresh and competing processes.
2. Give every expected source frame a session/generation, ordinal and original
   running PTS. Correlate sink receive/acquire, transport submit/IPC/acquire,
   adoption, actual scene sampling, GPU fence observation and present submission.
   Transport sequence is not interchangeable with source ordinal after a drop.
3. Every expected identity must have a valid stage chain and terminal disposition.
   Reject missing/truncated trace footers, trace loss, ambiguous identities,
   unexplained gaps, and missing repeated-draw completions/presents. A classified
   drop counts as a loss. Require known source count, not just observed frames.
4. Report startup, steady state and terminal drain separately **and** totals.
   Keep bounded overload negatives to prove reasons and lifetime safety. Preserve
   original A/V offsets while changing video scheduling.
5. Repeat matched BGRA/NV12 1080p30/60 native cases, minimum 60-second steady
   windows for qualification where the finite protocol permits. The current
   45-second identity captures are diagnostics and do not replace that gate.
   Run GPU benchmarks sequentially. No new tuning until residuals are classified.
6. For each demonstrated remaining mechanism, change one variable, retain the
   failing baseline/negative, and rerun the affected case. Do not broaden testing
   indefinitely once a causal question is answered.
7. Compare tracing disabled/enabled and native preview disabled/enabled. The
   accounting runs measure behavior; uninstrumented runs establish overhead.
   Report count completeness, interval p50/p95/p99/max, deadline error, latency,
   queue high water, CPU work and GPU waits separately. Fence observation is an
   upper bound on completion time, not an exact GPU-duration measurement.
8. Exit only with the required rate and **zero unexplained or avoidable losses**
   in the supported normal workload, bounded storage/latency, correct pixels,
   unchanged A/V timing, and passing recovery/lifetime tests. If a hardware or
   design limit remains, quantify it rather than silently changing the target.

Physical scanout feedback/capture is a separate measurement boundary. It must
be added before claiming every submitted image actually appeared on the panel;
it is not a blocker to fixing and qualifying producer-to-consumer timing and
application-level frame delivery now.

## Current execution

Four sequential native 45-second captures at the original 100 ms latency have
completed. Hardened identity accounting passes for every independently expected
fixture identity:

| Input | Inputs | Unique adopted/rendered/GPU-complete/present-submitted | Startup losses | Steady complete | End complete |
|---|---:|---:|---:|---:|---:|
| BGRA 1080p30 | 1,350 | 1,348 | 2 | 1,290/1,290 | 30/30 |
| NV12 1080p30 | 1,350 | 1,347 | 3 | 1,290/1,290 | 30/30 |
| BGRA 1080p60 | 2,700 | 2,694 | 6 | 2,580/2,580 | 60/60 |
| NV12 1080p60 | 2,700 | 2,691 | 9 | 2,580/2,580 | 60/60 |

All losses are explicitly classified as late, pending replacement or ring-full
in startup. Repeated scene draws are excluded. These are diagnostic finite
windows, not whole-clip losslessness, scanout, all-platform, or endurance
qualification. Raw evidence is retained in `/tmp/m5-native-accounted` on the
coordinator and `C:/Users/jonsu/dev/m5-native-accounted` on Caspar; the standalone
consumer drain and the above two defects still need final combined validation.

Cadence is checked independently of count completeness. From consecutive first
present-call-return observations for distinct steady frames, BGRA60 p50/p99/max are
16.668/16.932/16.991 ms; NV1260 are 16.666/16.867/16.960 ms. NV12 adoption has
more jitter (p99 19.247 ms), but this did not become missing identities or large
present-call gaps in the capture. These software observations do not certify
scanout. Times are recorded after the API returns, not at submission entry or
display latch. Method and all four cases: [cadence summary](frame-accounting/cadence-summary.json).

Parent integrated Linux checks passed: FrameLink **53/53**, shared renderer
**22/22**; latest accounting tests **17/17**. These are functional/regression
results before the two remaining fixes, not additional Windows rate evidence.

Parent owns integration and final checks. Windows worker owns combined startup/
presentation validation; startup worker owns preconnection edge-case review.
Accounting manifest work is complete and integrated; rejected presentation
attempt handling is being corrected. There is no missing user decision preventing this work.
M5 remains open; later milestones are not being used to bypass the gate.
