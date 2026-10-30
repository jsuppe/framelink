# FrameLink + renderer remaining roadmap

Updated 2026-10-30. Scope: local cross-platform video/audio sessions and renderer
integration. Historical cloud calling, unrelated receiver features and HDR are
not part of this release. Completed: FrameLink GPU/video baseline and renderer
PR11 integration merged. Audio prototype is implemented but not merged.

## M1 — Land the validated audio foundation
Deliverables: reviewed timestamp-preservation support, bounded A/V test fixture,
regression coverage, CI, merged master and accurate platform evidence.
Tasks:
- Integrate Windows evidence 2bf7ad8 and review fixes 0692e41 onto audio branch.
- Run final Windows timestamp/watchdog regressions and affected Linux/Android checks.
- Add automated coverage to CI, open PR, review and merge after checks pass.
- Reconcile stale plan/qualification entries against actual results.
Gate: passing checks for final revision; no pending review defects; merge verified.

## M2 — Define an interoperable A/V session contract
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
Deliverables: audio-aware renderer session input, explicit sync/latency policy,
clock-drift handling and measurable A/V diagnostics.
Tasks:
- Choose audio playback clock as default playback master; retain video-only low-latency mode.
- Implement bounded video hold/drop behavior and explicit audio underrun/discontinuity handling.
- Use GStreamer resampling/clock mechanisms for drift recovery instead of custom DSP.
- Define active audio selection/mute when multiple video sessions are visible; default one selected track.
- Expose queue depth, drops, underruns, drift, estimated A/V offset and reconnect generation.
- Test sustained drift, stalls, resume, late frames and device changes.
- Measure actual speaker/display alignment on available hardware separately from timestamp agreement.
Gate: fault injection demonstrates recovery and bounded queues; published measured sync results.

## M5 — Platform deployment and transport qualification
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
M1 -> M2 -> M3 -> M4 -> M5 -> M6 -> M7.
Platform investigation and test harness development may run in parallel after M2;
observability work can accompany implementation. Proposed soak lengths are release
checks, not claims of already completed testing. No fixed completion dates assigned.
