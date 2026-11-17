# M6 Windows final evidence review — 2026-11-17 (OPEN: one gap)

Candidate revision: `060c54e41bcbfe7ca8dbdef89e6e74fb2930935c`. Host for every row: the Windows test host in [M6_WINDOWS_READINESS.md](M6_WINDOWS_READINESS.md) (Windows 11 build 26300, RTX 5090, driver 32.0.16.1714, same adapter). Each evidence folder below has a README, the commands or runner, and file hashes.

## Advertised rows
| Row | Evidence | Built from | Status |
|---|---|---|---|
| Shared CPU, 4 cells, 1800 s | `cpu-path/windows-cpu-{nv12-1080p60,bgra1080p60,nv12-4k30,bgra4k30}-461584b` | `461584b` | pass, **older build** |
| Shared CPU lifecycle, 8 cells, 100 × 5 s | `cpu-path/windows-cpu-lifecycle-summary` | `a50841a`-equivalent source + test-only guard | pass, **older build** |
| Native two-hop, 3 cells, 1800 s (NV12 1080p60, NV12 4K30, BGRA 4K30) | `windows-native-final-duration-{nv12-1080,nv12-4k,bgra-4k}-a50841a` | `a50841a` | pass, **older build** |
| Native lifecycle, option B owner, 6 cells + BGRA1080 + held leases, 200 × 5 s, gate on cycles 100–199 | `lifecycle/device-owner-20261113/{native-cells-200,gate-200,extended-200}` | `f4185511` (candidate minus a header-only guard and the fault test) | pass |
| Integrated suite, exports, C11, old clients | `windows-integrated-final/runs/final-060c54e4` | `060c54e4` | pass, 180/180 |
| Hosted CI | PR #10 | `060c54e4` | pass |

## Limited or excluded rows (unchanged)
| Row | Status |
|---|---|
| Native BGRA 1080p60, 1800 s | **FAILED** at about 695 s on `a50841a`; excluded, not waived. Its option B lifecycle passes but doesn't change this |
| Decoded relay 1080p60 | short functional pair only (`77afa1a`); no endurance claim |
| Decoded relay 4K30 | **not qualified**; two 34 ms misses |
| Renderer submission, NV12 1080p60 | three 5 s captures (`a6a1906`); submission proof only |
| Legacy fresh-device native reconnect | **fails** the 100-cycle limit; unqualified. Use the device owner |
| Owner device-removal path | implemented, not tested |

## Gap: duration and shared-CPU evidence predates the owner code
- The 1800 s cells and the shared-CPU lifecycle were measured before option B landed. Between `a50841a` and the candidate, 10 production files changed (342 lines added) in `src/framelink_win.cpp`, the session transport/context, both A/V elements, the native relay pool and two headers.
- **Reviewed, not measured:** with no owner selected, the new code is skipped or equivalent.
  - `createChannel`/`openProducer` take the old `createDevice` branch.
  - The `flClose` owner block is guarded by `ch->attachment`.
  - The elements' owner fields stay null.
  - The only unconditional changes are an early `*out=nullptr` in channel creation and an extra null `shared_ptr` member.
- The checklist allows reusing older evidence "only with explicit hash/source equivalence". This is a reviewed functional equivalence, not hash or source equivalence. The 2026-11-12 execution-order rule also says endurance campaigns are batched on the final exact revision.
- **To close:** rerun the 7 duration cells (about 3.5 h) on the final revision, and optionally the 8 shared-CPU lifecycle cells (about 1.2 h). Alternatively, Jon accepts the reviewed equivalence explicitly.

Until then the checklist item "Final evidence review" stays open and M6 readiness is not recorded.
