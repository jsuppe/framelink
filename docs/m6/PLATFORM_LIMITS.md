# M6 platform and format limits

## Status — 2026-11-17

M6 is **ready for the approved Windows scope** and the Windows API is **frozen at `060c54e4`** (tag `m6-windows-api-freeze`), both recorded by Jon on 2026-11-17. Native BGRA and NV12 1080p60 are supported under a recorded host-scheduling assumption, without a clean 1800 s pass on the final revision. Decoded relay 4K30 stays excluded. M6 for Linux and Android is open and deferred, and M7 is held. Current status and revisions: [STATUS.md](../STATUS.md). The 2026-11-12 Windows scope approval and the closure checklist are in [M6 Windows readiness](M6_WINDOWS_READINESS.md).

Sections below that carry an earlier date describe status at that date.


Status: measured configurations, not universal hardware limits or final M6 qualification.

## Historical campaigns — not current advertised support

The older table below is historical. The approved readiness matrix above supersedes any broad pass interpretation, particularly native BGRA1080.

All target rows refer to 1920x1080 at60fps and3840x2160 at30fps, two transport slots, one unread frame, one output buffer, zero added playback latency, unsynchronized measurement appsinks. Frozen17/34ms arrival diagnostics are not isolated-hop budgets. Physical scanout and audio-device output are excluded.

| Platform | Format / consumer | Both target profiles | Evidence / remaining work |
|---|---|---|---|
| Windows | BGRA / native | 1800s counts,timing,steady resource gates pass | PRIMARY_RESULTS.md; lifecycle growth still fails |
| Windows | NV12 / native | 1800s selected captures pass | Historical159ms stall unresolved; no clean-repeat waiver |
| Windows | BGRA / CPU | 1800s counts,timing,steady resource gates pass | Bounded output-cache fix; final integrated rerun pending |
| Windows | NV12 / CPU | 1800s counts,timing,steady resource gates pass | Same acceptance exclusions |
| Linux | BGRA / native | 1800s counts,timing,steady resource gates pass | Native allocation wrapping; producer still uploads |
| Linux | NV12 / native | 1800s counts,timing,steady resource gates pass | Same producer-copy distinction |
| Linux | BGRA / CPU | Target throughput fails | 5s captures8/300 and11/150 delivered; M6.8 repair |
| Linux | NV12 / CPU | Target throughput fails | 5s captures23/300 and5/150; zero-fill/readcopy231/1148ms; M6.8 |
| Android | RGBA / native | 1800s counts,timing,steady resource gates pass | Exact-layout AHB wrapping; not native zero-copy producer proof |
| Android | NV12 / native | Unsupported exact native layout | Opaque YUV is not NV12 bytes; explicit rejection retained |
| Android | BGRA / native | Unsupported exact native layout | No false NV12/RGBA aliasing inferred |
| Android | NV12 / CPU | Target throughput fails after normalization repair | 10s599/600 and295/300; late arrivals; M6.8 |
| Android | RGBA/BGRA / CPU | Not target-rate qualified by this campaign | No inferred pass from portable pixel tests |
| All | RGB10 or other format/rate combinations | Not target-rate qualified by this campaign | Functional format tests are not throughput evidence |

See [primary results](PRIMARY_RESULTS.md), [software costs](cost/STAGE_COSTS.md), and [controlled stalls](cost/stall-signatures/README.md). All failures remain retained, including failed unsupported-layout attempts. No maximum sustainable rate is inferred from a failed target or short successful control.

## Negotiated shared-CPU path — separate qualification status

The older Windows CPU rows above refer to the native transport/readback path; they do not qualify the newer negotiated shared-CPU allocator path. Its independent results are:

| Candidate / format | 1080p60 | 4K30 |
| --- | --- | --- |
| BGRA shared CPU | **1800s pass at461584b**,108000video/86400000PCM, zero17ms misses; [evidence](cpu-path/windows-cpu-bgra1080p60-461584b/README.md) | **1800s pass at461584b**,54000video/86400000PCM, zero34ms misses, resource gates met; [evidence](cpu-path/windows-cpu-bgra4k30-461584b/README.md) |
| NV12 shared CPU | **1800s pass at461584b**,108000video/86400000PCM, zero17ms misses, resource slopes within gates; [evidence](cpu-path/windows-cpu-nv12-1080p60-461584b/README.md) | **1800s pass at461584b**,54000video/86400000PCM, zero34ms misses; [evidence](cpu-path/windows-cpu-nv12-4k30-461584b/README.md) |

These are transport arrival/accounting/resource results, not physical playback, native identity, or a waiver of earlier native-path failures. Source production code remains461584b; later evidence commits do not change its binaries.

## Cross-cutting limits and owners

- Windows repeated lifecycle growth exceeds16KB/cycle despite steady-state64KB/min passes. Device-lifetime integration/measurement: Evelyn; product acceptance: Jon. See lifecycle/WINDOWS_DEVICE_LIFETIME.md.
- Original Windows native NV124K30 stall remains unresolved. Source/post-push controls localize its first delay after measured source push, not to a specific GPU/driver/queue component. Evelyn resumes causal attribution with M6.6 joined evidence.
- Linux/Android controlled native GPU-delay injection unavailable (skip77); ordinary completion/death checks are not equivalent. M6.4 retains this gap.
- Legacy Windows renderer evidence uses100ms scheduling and hidden/no-active-display fixture.300 unique presentation submissions and zero-loss traces are not physical presentation or current primary renderer qualification. M6.9d remains open.
- CPU/GPU/isolated-hop product budgets and public overload default remain unresolved. No retrospective threshold adjustment. M6.6 must measure trace overhead and per-boundary identities; M6.7/9 require exact final integrated revision validation.

Final native a50841a two-hop cells: NV121080, NV124K and BGRA4K duration pass; BGRA1080 duration failed around695s (supported since 2026-11-17 by Jon's decision; see below). Older table passes retain historical scope and do not waive that failure. See LOW_LATENCY_PLAN.md for exact archives and unresolved cause.

**2026-11-17:** on the final revision `060c54e4`, native NV12 1080p60 also failed its 1800 s run (about 1298.7 s) with the same late-wake signature, while shared CPU 4/4 and native 4K30 2/2 passed ([results](windows-final-duration-060c54e4/README.md)). Jon approved native 1080p60 (NV12 and BGRA) as supported under a host-scheduling assumption, with diagnosis deferred; both failures stay recorded.

## Shared-CPU lifecycle completion — November12/13

All8format/target/persistent-role100x5s production lifecycle cells pass original warm resource limits. [Summary and exclusions](cpu-path/windows-cpu-lifecycle-summary/README.md). Native growth failures are not changed; no unlimited-rate reconnect claim.
