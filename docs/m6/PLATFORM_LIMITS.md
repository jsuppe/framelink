# M6 platform and format limits

Status: measured configurations, not universal hardware limits or final M6 qualification.

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

## Cross-cutting limits and owners

- Windows repeated lifecycle growth exceeds16KB/cycle despite steady-state64KB/min passes. Device-lifetime integration/measurement: Evelyn; product acceptance: Jon. See lifecycle/WINDOWS_DEVICE_LIFETIME.md.
- Original Windows native NV124K30 stall remains unresolved. Source/post-push controls localize its first delay after measured source push, not to a specific GPU/driver/queue component. Evelyn resumes causal attribution with M6.6 joined evidence.
- Linux/Android controlled native GPU-delay injection unavailable (skip77); ordinary completion/death checks are not equivalent. M6.4 retains this gap.
- Legacy Windows renderer evidence uses100ms scheduling and hidden/no-active-display fixture.300 unique presentation submissions and zero-loss traces are not physical presentation or current primary renderer qualification. M6.9d remains open.
- CPU/GPU/isolated-hop product budgets and public overload default remain unresolved. No retrospective threshold adjustment. M6.6 must measure trace overhead and per-boundary identities; M6.7/9 require exact final integrated revision validation.
