# Windows remaining execution dependencies — 2026-11-12

After collection of the missing shared-CPU BGRA1080 cell, no runtime is active.
All four frozen461584b shared-CPU cells are collected passes. Exacta50841a178/178,
128exports and3 unchanged clients are already collected; do not repeat them.
Controlled Windows GPU-delay, existing-policy metadata/audio faults, held-reader,
pause/death/cancellation functional checks already exist in that suite.

## Concrete next dependency: current renderer source selection

M6.6a/9d current-renderer integration is distinct from the legacy presentation
probe. Existing evidence explicitly uses100ms scheduling and no active display;
it proves submission identities, not current primary-mode acceptance or scanout.
Read-only source lookup on November12 found:

- <win-home>/dev/vkrender-m5-final-validation has the historical av_input.h /
  av_schedule.h source and built renderer, but no Git repository identity.
- <win-home>/dev/vkrender is ed03cb8466d9010d2861dc51c6a65790dd3853c7 and
  lacks that av_input.h path; an unrelated untracked file is preserved.
- <linux-home>/vkrender-src is983d1ea with unrelated dirty tracked/untracked
  work. No edits made. The local bare mirror masterc71e013 is not the historical
  M5 source. Searching local available files found no av_input.h/av_schedule.h.

Do not silently replace an unrelated current checkout, claim the old binary is
current, or choose a new renderer architecture from these different baselines.
**Parent/project coordination supplies/selects the authoritative M6 renderer
revision/tree**, or explicitly selects the legacy source tree for a hash-pinned
rebuild. Evelyn then owns isolated source capture, contract/clock integration
review, focused build and submission-identity checks. No physical-output claim
or new performance budget is inferred. This is the next concrete source-selection
question, not a request to reapprove routine implementation.

## Other dependencies, owners and resume conditions

| Item | Owner | Resume condition |
| --- | --- | --- |
| Device-lifetime alternative | Jon: supported contract choice; Evelyn: implementation/measurement | Decide whether explicit additive opt-in owner is useful while unchanged default remains failed; review DEVICE_LIFETIME_OPTION.md. No cache/sleep workaround is justified |
| D2 overload / D3 convenience fallback | Jon | Explicit product/default decision; current entrypoint defaults remain unchanged |
| Native BGRA1080 natural323ms stall | Evelyn causal investigation | Original-scale phase/thread/I/O evidence localizing the natural delay; synthetic pause proves catch-up mechanism only. No uninstrumented duration retry |
| Decoder4K misses | Evelyn causal investigation | Evidence localizing original source-oracle/timing outlier, which isolated phase control did not reproduce; no clean-repeat waiver or decoder endurance |
| Historical startup/159ms native timing | Evelyn causal investigation | Matched original-scale phase/wait evidence plus compatible remedy, not another equivalent clean control |
| Default-profile lifecycle slopes | Evelyn/backend/driver; Jon only for contract changes | Real remedy plus unchanged matched production gates. New deferred-reclaim probes do not qualify default profile |
| Primary-renderer/isolated-hop performance acceptance | Product budget review plus renderer integration | Authoritative renderer baseline and frozen applicable budgets; API submission is not physical scanout |
| Linux/Android integration and campaigns | Project scope/Jon | Windows-first deferral lifted or required focused shared-code check; no broad campaign is authorized as a workaround for Windows blockers |
| Final M6 closure/CI/API freeze | Evelyn after predecessors | Resolved applicable gates/decisions and final exact changed-runtime qualification; M7 remains held |

No additional supported Windows missing-cell duration or unrun existing-policy
functional check was identified in this reconciliation. This does not assert
that all project work is mathematically impossible; it identifies why the next
renderer implementation cannot honestly be labelled active before baseline
selection. Do not launch speculative repeats merely to keep a process running.
