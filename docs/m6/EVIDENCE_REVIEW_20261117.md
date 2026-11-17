# M6 Windows final evidence review — 2026-11-17 (CLOSED by Jon's decisions)

Candidate revision: `060c54e41bcbfe7ca8dbdef89e6e74fb2930935c`. Host for every row: the Windows test host in [M6_WINDOWS_READINESS.md](M6_WINDOWS_READINESS.md) (Windows 11 build 26300, RTX 5090, driver 32.0.16.1714, same adapter). Each evidence folder below has a README, the commands or runner, and file hashes.

## Advertised rows
| Row | Evidence | Built from | Status |
|---|---|---|---|
| Shared CPU, 4 cells, 1800 s | `windows-final-duration-060c54e4` (earlier: `cpu-path/windows-cpu-*-461584b`) | `060c54e4` | pass, 4/4 on the final revision |
| Shared CPU lifecycle, 8 cells, 100 × 5 s | `cpu-path/windows-cpu-lifecycle-summary` | `a50841a`-equivalent source + test-only guard | pass, **older build** |
| Native two-hop NV12 4K30 and BGRA 4K30, 1800 s | `windows-final-duration-060c54e4` (earlier: `windows-native-final-duration-{nv12-4k,bgra-4k}-a50841a`) | `060c54e4` | pass, 2/2 on the final revision |
| Native two-hop NV12 1080p60, 1800 s | `windows-final-duration-060c54e4` (earlier pass: `windows-native-final-duration-nv12-1080-a50841a`) | `060c54e4` | **FAILED** at about 1298.7 s on the final revision; the `a50841a` pass does not override it. Status is Jon's decision |
| Native lifecycle, option B owner, 6 cells + BGRA1080 + held leases, 200 × 5 s, gate on cycles 100–199 | `lifecycle/device-owner-20261113/{native-cells-200,gate-200,extended-200}` | `f4185511` (candidate minus a header-only guard and the fault test) | pass |
| Integrated suite, exports, C11, old clients | `windows-integrated-final/runs/final-060c54e4` | `060c54e4` | pass, 180/180 |
| Hosted CI | PR #10 | `060c54e4` | pass |

## Limited or excluded rows
| Row | Status |
|---|---|
| Native BGRA 1080p60, 1800 s | **FAILED** at about 695 s on `a50841a`. Since Jon's 2026-11-17 decision it is **supported** under the host-scheduling assumption (see Decisions); the failure stays recorded |
| Decoded relay 1080p60 | short functional pair only (`77afa1a`); no endurance claim |
| Decoded relay 4K30 | **not qualified**; two 34 ms misses |
| Renderer submission, NV12 1080p60 | three 5 s captures (`a6a1906`); submission proof only |
| Legacy fresh-device native reconnect | **fails** the 100-cycle limit; unqualified. Use the device owner |
| Owner device-removal path | implemented, not tested |

## Duration rerun on the final revision (2026-11-17)
The seven 1800 s cells were rerun on `060c54e4`: [results](windows-final-duration-060c54e4/README.md).
- **Closed:** shared-CPU duration (4/4 pass) and native 4K30 duration (2/2 pass) are now measured on the final revision.
- **Failed:** native NV12 1080p60 stopped at about 1298.7 s. The test source's video thread woke 43.5 ms late, outside any FrameLink call, and the catch-up burst hit the queue-1 replacement. This is the same signature as the excluded BGRA 1080p60 failure on `a50841a`, which predates the owner code. The cause of the late wake-up is not established.
- No retry and no waiver were applied.

## Decisions — Jon, 2026-11-17
- **Native 1080p60 (NV12 and BGRA): supported.** Root-cause testing is deferred; the working assumption is a host scheduling stall, not FrameLink ([decision and deferred plan](windows-final-duration-060c54e4/README.md#decision--jon-2026-11-17)). Both 1800 s failures stay recorded; neither format has a clean 1800 s pass on the final revision.
- **Shared-CPU lifecycle, 8 cells:** the reviewed functional equivalence is accepted (with no owner selected the new code is skipped). Not remeasured on the final revision.
- **API frozen at `060c54e4`**, D2/D3 deferred.

With these decisions the review is closed and M6 readiness for the Windows scope is recorded in [M6_WINDOWS_READINESS.md](M6_WINDOWS_READINESS.md). M7 stays held.
