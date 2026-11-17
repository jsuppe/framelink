# Windows remaining execution dependencies — updated 2026-11-17

## Explicit retained-device owner approved — 2026-11-12 20:40:45 EST

Jon selected option B for persistent applications. Implement and qualify the explicit
selected-adapter retained-device owner described in DEVICE_LIFETIME_OPTION.md:
create/retain/release, one attached channel initially, explicit factories/bounded
producer open, fresh generation storage/fences/identity and transactional session/
GStreamer propagation. Safe references, leases and graph retirement remain mandatory;
removed devices reject. Legacy APIs/defaults remain; no hidden cache/fallback or
waiver of failed default lifecycle. Require original-limit matched100-cycle both-role
and held-lease tests, faults, compatibility and final changed-runtime qualification.
This supersedes only prior device-owner deferral/unapproved status. Other scope
exclusions, unchanged D2/D3/budgets, serial execution and M7 hold remain.
Loom: REQ-1dcc0902 (owner), REQ-2d7aec46 (preserved/amended Windows scope);
REQ-5c4df445 is superseded, not a waiver of its other constraints.


## Status — 2026-11-17

M6 is **ready for the approved Windows scope** and the Windows API is **frozen at `060c54e4`** (tag `m6-windows-api-freeze`), both recorded by Jon on 2026-11-17. Native BGRA and NV12 1080p60 are supported under a recorded host-scheduling assumption, without a clean 1800 s pass on the final revision. Decoded relay 4K30 stays excluded. M6 for Linux and Android is open and deferred, and M7 is held. Current status and revisions: [STATUS.md](../STATUS.md). The 2026-11-12 Windows scope approval and the closure checklist are in [M6 Windows readiness](M6_WINDOWS_READINESS.md).

Sections below that carry an earlier date describe status at that date.


All four frozen461584b shared-CPU duration cells and all8target/role production lifecycle cells are collected passes; Windows runtime is idle after01:23:12ZNovember13. Exacta50841a178/178,
128exports and3 unchanged clients are already collected; do not repeat them.
Controlled Windows GPU-delay, existing-policy metadata/audio faults, held-reader,
pause/death/cancellation functional checks already exist in that suite.

## Resolved dependency: current renderer source selection

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
**Resolved by approved plans and GitHub PR13:** merged9f74b9d is the existing
approved baseline. Clean isolated source and137-file historical comparison are
[pinned here](renderer-provenance-9f74b9d/README.md). No new Jon architecture
choice is needed. Isolated integration is now collected: renderer repaira6a1906,
4focused tests,9CLI controls and three complete5s source-to-submission captures pass.
See [integration evidence](renderer-integration-a6a1906/README.md). No physical-output
claim, new performance budget or renderer endurance qualification is inferred.

## Dependencies, owners and state — 2026-11-17

Evelyn's earlier ownership of investigations ended with her usage pause on 2026-11-13; remaining items are owned by whoever is assigned when Jon resumes them.

| Item | State (2026-11-17) | Resume condition |
| --- | --- | --- |
| Supported bounded lifecycle | **Done.** Shared CPU 8/8; native through the retained device owner (option B) under the cycles 100–199 rule | — |
| D2 overload / D3 convenience fallback | **Deferred by Jon** until a convenience entry point is proposed | A proposed convenience entry point |
| Native 1080p60 late wake-up (BGRA on `a50841a`, NV12 on `060c54e4`) | **Supported by Jon's decision** under a host-scheduling assumption; diagnosis deferred | Jon resumes it; [diagnostic plan](windows-final-duration-060c54e4/README.md#decision--jon-2026-11-17) |
| Decoder 4K misses | Excluded; cause not established | Evidence localizing the original source/timing outlier; no clean-repeat waiver |
| Historical startup/159 ms native timing | Cause not established | Matched original-scale phase/wait evidence plus a compatible remedy |
| Default-profile (fresh-device) lifecycle slopes | Unqualified; not needed for support because the owner profile is the supported path | A real remedy plus unchanged matched production gates |
| Renderer supported-profile acceptance | Submission proof only: NV12 0/100 ms, ring 12/pending 6, explicit CPU conversion | Named intended support, with existing budgets |
| Owner device-removal path | Implemented, untested | A device-removal test on the owner |
| Linux/Android integration and campaigns | Deferred | Jon chooses to extend M6 to Linux/Android |
| Final M6 closure/CI/API freeze | **Done.** API frozen at `060c54e4`; M6 ready for the Windows scope; merged as PR #10 | — (M7 held) |

## Native lifecycle contract choice (historical)

Jon selected option B (the retained device owner) at 20:40:45 EST on 2026-11-12. It is implemented and qualified; see [native lifecycle choices](lifecycle/NATIVE_LIFECYCLE_DECISION.md) for the alternatives that were considered.
