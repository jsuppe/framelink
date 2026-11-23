# Windows native 4K30 on a quieted host — 2026-11-22

**Why.** The same-day [A/B](../windows-4k-ab-20261122/README.md) showed that the frozen `060c54e4` binaries also fail NV12 4K30 on today's host, so the 4K30 failures at `0e8da988` ([record](../windows-native-relay-rate-0e8da98/README.md)) point at the host. With the owner's approval, the host was quieted and the same tests run again:
1. the A/B, frozen arm then current arm (`ab4k-quiet.ps1`, same command as before);
2. both formal 4K30 cells at `0e8da988`, through the rate runner (`run_local_native_rate_cells.py --cells bgra4k30 nv12-4k30 --duration 1800`).

This is a **new host configuration**. It does not replace the earlier failures, which stand as recorded.

**Host changes.** These applications were closed before the run: Ollama (its service stopped for the session, not disabled), LM Studio, NVIDIA Broadcast, Steam, and the Epic Games launcher with its overlay. Nothing was uninstalled and no startup setting was changed. Browsers, Discord, VS Code and the Claude app stayed open. The script refuses to start unless those applications are gone. It checked again after the A/B and after the cells: "closed apps still closed" both times (`run.log`). `host-state.txt` lists the processes and GPU clients before and after closing them. The driver (617.14), binaries and pins are unchanged.

## Results

| Run | Result | Video arrival p50 / p99 / max (ms) | Audio arrival p99 / max (ms) |
|---|---|---|---|
| A/B frozen `060c54e4`, NV12 4K30 | **FAIL** at 80.5 s: the source woke 196 ms late for frame 2415; the catch-up lost a frame at the relay (`fl_gst_local_native_bench_win.cpp:90`) | | |
| A/B current `0e8da988`, NV12 4K30 | **FAIL** (audio): 1800 s complete, exit 0, every frame delivered, but the receiver's PCM thread stalled 196 ms at 6.3 s, and 17 blocks (632–648) arrived 196 → 36 ms late | max 10.68 | max **195.75** |
| Cell `bgra4k30` 1800 s (after preflight and trace pass) | **pass**, zero misses | 10.14 / 11.71 / 15.61 | 0.23 / 3.21 |
| Cell `nv12-4k30` 1800 s (after preflight and trace pass) | **pass**, zero misses | 6.23 / 9.08 / 10.24 | 0.24 / 6.53 |

In the A/B current arm, the relay received every audio block on time (max 2.72 ms). Video frames in the receiver around the stall were on time (5–9 ms): only its PCM thread stalled. That is the receiver-PCM signature from the [formal record](../windows-native-relay-rate-0e8da98/README.md#the-receiver-audio-stalls-are-4k-only), but four times as long.

Formal cells, per role (source / relay / receiver):
- CPU: BGRA 27 % / 10 % / 14 %, NV12 22 % / 5 % / 10 %.
- Resource slopes: relay and receiver memory within ±0.5 KB/min, handles and threads ≤ 0.03/min. The **source** memory slope is 42.0 KB/min (BGRA) and 43.9 KB/min (NV12). That is under the 64 KB/min gate, and matches the 42.9 KB/min of the failed BGRA cell earlier today, but is far above the 1080p60 cells (≤ 0.12 KB/min). Its cause is not established.

## Both A/B stalls coincide with Windows Update restore points

Both stalls are 196 ms long (195.84 and 195.75 ms), 8.6 s apart in wall-clock time. Converted from QPC using the capture files' write times (±1 s), they fall at about **20:47:54.8** (frozen) and **20:48:03.4** (current).

In the same seconds, Windows Update was installing Store app updates (Windows Terminal, Xbox Identity Provider, Xbox Gaming Overlay). Each install created a System Restore point through the Volume Shadow Copy service (`events-2047-2048.txt`):

| Shadow copy | VSS snapshot initiated | Restore point created | Stall |
|---|---|---|---|
| 13 | 20:47:51.770 | 20:47:55.802 | ≈ 20:47:54.8 (frozen arm, source video thread) |
| 14 | 20:48:01.571 | 20:48:04.480 | ≈ 20:48:03.4 (current arm, receiver PCM thread) |

These are the **only two** VSS snapshots on the host between 15:00 and 23:59 today. Both stalls fall inside a snapshot's window, about 1 s before the restore point was reported. The formal cells ran from 21:18 to 22:18, with no snapshot, and passed.

This is a correlation, not an established mechanism, and it does not explain the noisy-host failures earlier today: no VSS snapshot happened then. A plausible path is the snapshot's write freeze. Both stalled threads write a CSV row per frame or block. The source's earlier 474 ms stall was also between its submit and its next acquire, where the thread writes its CSV row. Confirming it would need per-phase stamps around the CSV writes, or a run with System Restore disabled. Both are the owner's decision.

## Verdict, for Jon

- **The formal 4K30 cells pass on the quieted host**: 2 of 2 at 1800 s, zero video and audio misses, with arrival max 15.61 ms (BGRA) and 10.24 ms (NV12) against 34 ms.
- **The two A/B runs on the same quieted host both fail**, one per binary, each on a single 196 ms stall coinciding with a Windows Update restore point. No repeat replaces them.
- So on this host, 4K30 over 1800 s passes when nothing interrupts it. Today there were eight Windows 4K 1800 s attempts: six failed, two passed. Where their large stalls fell:
  - the source's video thread between submit and the next acquire (474 ms, then 196 ms);
  - the receiver's PCM thread (up to 54 ms, then 196 ms);
  - on the noisy host only, two GPU-facing calls: the source's `flSubmit` (120 ms) and the receiver's acquisition (170 ms or more).

  None was in the relay's forward, which stayed at p99 ≤ 0.07 ms.
- Whether Windows 4K30 counts as qualified — on the formal cells, on a host with System Restore or Store updates paused, or only after a mechanism is shown — is Jon's call.

## Restoring the host

The closed applications come back at the next login or reboot; Ollama's service is still set to Automatic. To bring them back now, start them from the Start menu, and Ollama with `Start-Service ollama` (elevated).

## Files

- `run.log`: the script's output (pin check, quiet check, arm times, cell verdicts, re-checks).
- `ab4k-quiet.ps1`: the script. `events.ps1`: the event-log query, with its output in `events-2047-2048.txt`.
- `host-state.txt`: process and GPU-client names before and after quieting (the full lists, with paths, are in the archive under `quiet/`).
- Per A/B arm: `config.json`, role summaries where written, and the non-empty stderr.
- `cells-0e8da98-quiet/`: `summary.json`, `environment.json`, each capture's `.analysis.json`, and per capture `config.json` and role summaries.
- The raw captures (per-role CSVs for both arms and all six cell captures) are 21 MB compressed, kept outside the repository: `captures.tar.xz`, SHA-256 in `captures.tar.xz.sha256`.
