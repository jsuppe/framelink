# M6 platform and format limits

## Approved M6 Windows readiness scope — 2026-11-12 18:14 EST

Jon approved readiness for explicitly named Windows configurations, not universal
Windows support. This supersedes older broad M6 completion requirements below;
historical results and failed gates remain evidence, not silently passed tasks.
Authoritative reviewable scope/checklist: [M6 Windows readiness](M6_WINDOWS_READINESS.md).
M6 remains OPEN; M7 maintenance hold remains. Correct media/timeline/ownership,
fault handling, validated bounded lifecycle and final exact-revision integrated/API
compatibility gates remain mandatory. No new budget, D2/D3 default or device-owner
API approval is inferred. Failed native BGRA1080 and decoder4K are NOT QUALIFIED
and excluded from initial advertised support. One task and one executor, serially.


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

Final native a50841a two-hop cells: NV121080, NV124K and BGRA4K duration pass; BGRA1080 duration failed around695s and remains unqualified. Older table passes retain historical scope and do not waive that failure. See LOW_LATENCY_PLAN.md for exact archives and unresolved cause.
