# FrameLink + renderer remaining roadmap

Updated 2026-10-30. Scope: local cross-platform video/audio sessions and renderer
integration. Historical cloud calling, unrelated receiver features and HDR are
not part of this release. Completed: FrameLink GPU/video baseline and renderer
PR11 integration merged. Audio prototype is implemented but not merged.

## M1 — Land the validated audio foundation

Status: COMPLETE. PR #2 merged 2026-10-30 as `c67cde8` after hosted
Linux and Windows CI passed (run `41048544244`). Runtime source `180e0a2`: Linux 30/30, Windows 31/31; Android
arm64/x86_64 builds and Pi/tablet native normal/fault checks passed. Hosted CI
checks builds, six CPU tests and A/V test registration; GPU runtime evidence
is from the documented real-device runs, not hosted runners.

Deliverables: reviewed timestamp-preservation support, bounded A/V test fixture,
regression coverage, CI, merged master and accurate platform evidence.
Tasks:
- Integrate Windows evidence 2bf7ad8 and review fixes 0692e41 onto audio branch.
- Run final Windows timestamp/watchdog regressions and affected Linux/Android checks.
- Add automated coverage to CI, open PR, review and merge after checks pass.
- Reconcile stale plan/qualification entries against actual results.
Gate: passing checks for final revision; no pending review defects; merge verified.

## M2 — Define an interoperable A/V session contract

Status: COMPLETE. PR #3 merged 2026-10-30 as `1b02523` after Linux and
Windows hosted CI passed (run `41050444424`).
Final Linux suite 33/33 and Windows 34/34 passed. Android arm64/x86_64 builds
and Pi/tablet contract/C11 checks passed. Two independent desktop contract peers
validate agreement and rejection; production offer exchange, authenticated
rendezvous and media applications remain M3/M5, not claimed implemented here.

Deliverables: versioned session specification, descriptor/control API, negotiation
state machine and contract tests; image-only flFrame API preserved.
Tasks:
- Define session/track IDs, protocol version, config generation and endpoint lifetime.
- Define audio/video capability offer, intersection, selection and rejection.
- Start PCM with S16/F32, mono/stereo and explicit rate/channel layout; prefer48kHz.
- Define clock domain, epoch mapping, first-sample PTS, duration and discontinuities.
- Define bounded buffering, timeout, backpressure and late-frame policies per track.
- Specify local endpoint permissions/authentication and cleanup behavior.
- Test incompatible formats/versions, stale generation, malformed descriptors and time bounds.
Gate: separately launched peers agree on formats and timeline; incompatible peers fail clearly.

## M3 — Independent producer and consumer applications

Status: COMPLETE. PR #4 merged 2026-10-30 as `148cc05` after hosted
Linux and Windows CI passed (run `41053902861`). Final Linux
38/38 and Windows39/39 passed, including ten-case startup/recovery/fault matrices.
Optional --play exercised on Linux virtual X display + silent sink for BGRA/NV12;
physical playback synchronization and Android application deployment remain M4/M5.

Deliverables: standalone A/V producer/consumer modes, session discovery/connection,
headless verifier and real playback option; existing video-only modes preserved.
Tasks:
- Add session/audio/video CLI options to producer and consumer samples.
- Implement independent start order, endpoint exchange and shared epoch mapping.
- Transport video through FrameLink and PCM through the selected existing audio path.
- Support synthetic video/tone, timestamps and discontinuity controls.
- Add consumer RGB/NV12 display plus audio playback and silent verification modes.
- Exercise producer/consumer restart, reconnect, format changes and one-track failure.
- Build a two-process integration harness that checks pixels, PCM and timeline.
Gate: separate OS processes exchange synchronized A/V in either start order and recover safely.

## M4 — Playback synchronization and renderer integration

Status: COMPLETE, qualified scope. FrameLink PR5 ed81cbb and renderer PR12
4c2ff15 merged after all final CI passed. Linux47/47, renderer20/20, Windows49/49.
Physical qualification and external decoded pipeline handoff remain M5.
Deliverables: audio-aware renderer session input, explicit sync/latency policy,
clock-drift handling and measurable A/V diagnostics.
Tasks:
- Choose audio playback clock as default playback master; retain video-only low-latency mode.
- Implement bounded video hold/drop behavior and explicit audio underrun/discontinuity handling.
- Use bounded source queue/backpressure or joint-session re-anchor for source pacing
  faults; qualify GStreamer device-clock slaving separately, without claiming
  seamless adaptive independent-source resampling.
- Define active audio selection/mute when multiple video sessions are visible; default one selected track.
- Expose queue depth, drops, arrival drift and reconnect generation; expose
  unmeasured device underruns, oscillator drift and physical A/V skew as unavailable.
- Test sustained drift, stalls, resume, late frames and device changes.
- Measure actual speaker/display alignment on available hardware separately from timestamp agreement.
Gate: fault injection demonstrates recovery and bounded queues; publish measured
transport-offset/scheduling results. Physical speaker/display alignment remains
a separate M5 gate under the clarified producer-to-consumer timing requirement.

## M5 — Platform deployment and transport qualification

Status: IN PROGRESS (authorized 2026-10-30). First deliverable: Windows ordinary
GStreamer decoded A/V pipeline -> separate FrameLink consumer example and guide.
Deliverables: tested Linux/Windows apps, Android APK/service integration, capability
matrix and transport decision record.
Tasks:
- Qualify Linux and Windows actual audio sinks/display paths, not only appsinks.
- Implement Android lifecycle, permissions and supported cross-app/service session ownership.
- Test Android audio focus, background/foreground, surface recreation and route changes.
- Test device absence, unsupported formats, suspend/resume and resource cleanup.
- Measure existing audio transport latency/copy cost and deployment constraints.
- Only if evidence shows an unmet requirement: implement separate bounded PCM transport,
  preserving session contract; otherwise retain GStreamer transport.
- Track Pi/Cuttlefish GPU format/fence limitations and Linux physical display validation separately.
Gate: each advertised feature has implementation plus platform evidence or explicit unsupported status.

## M5a — Memory-capability negotiation and GPU-native pipeline integration

Status: PLANNED, approved by Jon 2026-10-30. Separate milestone after M5 and
before M6; existing milestone numbers remain stable. Does not expand M4.

Deliverables: versioned memory/usage negotiation contract, platform capability
adapters, GStreamer allocation integration, Windows external-pipeline example,
and evidence identifying actual copies/conversions and unsupported paths.

Tasks:
- Define required versus preferred usage: GPU sampling/render targets, decoder
  output, CPU read/write; do not equate API usage with physical memory residency.
- Advertise supported resource types, format/plane layout, strides/modifiers,
  device identity, import/export and synchronization capabilities. Map Windows
  D3D resources, Linux DMA-BUF and Android AHardwareBuffer only where supported.
- Negotiate a compatible allocation strategy, ownership and buffer-pool lifetime;
  specify producer-owned versus consumer-provided allocations and bounded queues.
- Define explicit fallback policies: allow copy/conversion or require zero-copy;
  reject incompatible requirements clearly. Preserve legacy defaults and negotiate
  protocol compatibility with older peers rather than silently changing behavior.
- Connect remote consumer requirements to GStreamer caps memory features,
  ALLOCATION queries and buffer pools; renegotiate safely after format/device or
  session-generation changes. Preserve A/V timestamps and existing session sync.
- Implement the Windows hardware-decoder -> GPU GstBuffer -> FrameLink sink ->
  independent consumer renderer example; document tested decoder/adapter/format.
- Implement CPU-readable consumer paths with explicit staging/readback where
  required. Multiple consumers with conflicting requirements must have a defined
  allocation/copy strategy or explicit rejection, not an implicit global preference.
- Expose selected memory type, usage, device, mapping behavior, fallback reason,
  and observed copy/conversion counters; distinguish transport sharing from
  end-to-end zero-copy, and unknown residency from measured residency.
- Add contract/unit tests for preference selection, hard requirement rejection,
  version compatibility, malformed offers and conflicting consumer requirements.
- Add platform integration tests for GPU-native and CPU-readable modes, forced
  fallback/strict-zero-copy rejection, unsupported formats/device mismatch,
  renegotiation, fencing, stale generations, cleanup and unchanged A/V timing.
- Benchmark supported paths and publish the platform support matrix and developer
  integration guide. Unsupported driver paths remain explicitly capability-gated.

Gate: selected allocation satisfies all required usage constraints; permitted
fallbacks are visible; strict zero-copy never silently copies. Pixel/timestamp
correctness, safe ownership/fencing and bounded resource use pass on advertised
platform paths. Zero-copy claims require path evidence, not merely GPU buffer type.

## M6 — Reliability, performance and observability
Deliverables: reproducible stress/benchmark suite, opt-in tracing, measured overhead
and release acceptance report.
Tasks:
- Run 30-minute A/V soaks and >=100 restart/reconnect cycles per supported target.
- Exercise slow consumers, audio underruns, video drops, generation changes and endpoint death.
- Record latency distributions, queue sizes, CPU/GPU/memory/handle counts and clock drift.
- Compare logging disabled/enabled and detect trace record loss.
- Add deterministic fault regression tests and CI where hardware-independent.
- Set performance acceptance thresholds from measured baselines before release qualification.
Gate: no unexplained corruption/hangs/unbounded growth; documented performance envelope and limitations.

## M7 — Release and handoff
Deliverables: versioned release, build artifacts, API documentation, runnable examples
and completed requirements/validation matrix.
Tasks:
- Final review, compatibility/migration notes and clean-install checks.
- Publish producer/consumer invocation guides and troubleshooting metrics.
- Verify packaged dependencies, platform artifacts and CI on final revisions.
- Merge approved work, tag release and publish evidence/known limits.
Gate: a clean machine can reproduce the documented A/V workflow; all release-scope tasks closed
or explicitly capability-gated, with unqualified hardware behavior never labeled passed.

## Execution dependencies
M1 -> M2 -> M3 -> M4 -> M5 -> M5a -> M6 -> M7.
Platform investigation and test harness development may run in parallel after M2;
observability work can accompany implementation. Proposed soak lengths are release
checks, not claims of already completed testing. No fixed completion dates assigned.
