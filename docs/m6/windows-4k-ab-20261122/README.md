# Windows native NV12 4K30: frozen versus current harness, same host — 2026-11-22

**Why.** Both Windows 4K30 cells failed their 1800 s runs at `0e8da988` ([record](../windows-native-relay-rate-0e8da98/README.md)), although both passed 1800 s on the frozen revision `060c54e4` on 2026-11-17 ([record](../windows-final-duration-060c54e4/README.md)). This A/B separates a harness change from a host change: the same cell, NV12 3840×2160 at 30 fps for 1800 s, once with the frozen binaries and once with the current ones, back to back on the same host under the Windows test mutex.
- **Frozen arm:** the `060c54e4` binaries built on 2026-11-16, with SHA-256 checked against the frozen record's pins before the run (`run.log`: "frozen pins match").
- **Current arm:** the `0e8da988` binaries from the formal cells.
- Command for both: `fl_gst_local_native_bench_win.exe gstframelink.dll --out <dir> --seconds 1800 --width 3840 --height 2160 --fps 30 --format NV12` (`ab4k.ps1`).

## Result: both arms fail

| Arm | Stopped at | Frames before the stop | What happened |
|---|---|---|---|
| Frozen `060c54e4` | 986 s | 29 580 | The source's `flSubmit` for frame 29 580 took 120 ms (wake-up, acquire and upload on time). The catch-up burst lost a frame at the receiver (`fl_gst_local_native_bench_win.cpp:75`). |
| Current `0e8da988` | 1.9 s | 51 | The receiver stopped acquiring after frame 50 for at least 170 ms. The source's acquire then waited 167 ms for a free buffer, and the relay lost a frame (`fl_gst_local_native_bench.cpp:344`). |

**The frozen binary, which passed this cell on 2026-11-17, now fails on the same host.** So the 4K30 failures at `0e8da988` cannot be attributed to the harness change. The host's behavior has changed.

The stalls are not in one place. Across today's 4K runs they hit:
- the source's submit (120 ms);
- the receiver's acquisition (170 ms or more, and the 50 ms stall at 1555 s in the formal BGRA cell);
- the source's video wake-up (474 ms in the formal NV12 cell);
- the receiver's PCM thread (30–54 ms, formal NV12 cell).

The frozen arm had no receiver-only PCM stalls in its 986 s. That is one run, not a difference between the harnesses.

## Host state at the time

The host is a desktop in daily use. During the runs, these GPU clients were active (`nvidia-smi --query-compute-apps`): Ollama, LM Studio, NVIDIA Broadcast, Steam, the Epic Games launcher and its overlay renderer, Discord, Chrome, Firefox, Edge WebView, VS Code and the Windows shell. The same applications were already running on 2026-11-12 (`../windows-startup-phase-461584b/processes-before.json`), so their presence is not new; their activity may be. `ollama ps` showed no loaded model afterwards.

The driver is unchanged (617.14). The last OS updates were installed on 2026-11-15, before the frozen run. Over a 60 s idle sample after the runs, the GPU sat mostly in P5 (low clocks), switching between P5, P3 and P1 every few seconds.

**Not established:** what stalls the GPU-facing calls. Candidates are contention from the other GPU clients, GPU power-state transitions (4K30 leaves the GPU idle most of each frame interval), and OS background work. Separating them needs changes to the host: closing the other GPU clients, or pinning the GPU's power state. Those are the owner's decision.

## Files

- `run.log`: the script's output, with the pin check and the start and end time of each arm.
- `ab4k.ps1`: the script.
- Per arm: `config.json` and the non-empty stderr.
- The raw captures (per-role CSVs) are 3.6 MB compressed, kept outside the repository: `captures.tar.xz`, SHA-256 in `captures.tar.xz.sha256`.
