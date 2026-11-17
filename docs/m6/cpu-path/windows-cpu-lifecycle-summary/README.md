# Windows shared-CPU production lifecycle — 8/8 cells PASS

Same measured Windows11Home26300/RTX5090driver32.0.16.1714 host. Explicit shared
CPU, pool2, unread1, output queue1, zero configured latency; no native fallback.
Frozen production461584b DLLs; test-only a5d52d1 permits existing shared-CPU bins
in the previously rejected lifecycle benchmark mode. No production/API/default change.

Each of BGRA/NV12 x1920x1080p60/3840x2160p30 has both persistent-consumer/fresh-producer
and persistent-producer/fresh-consumer100x5s runs, preceded by a2cycle control.
800 measured cycles:180000video/192000000PCM frames; all exact counts.
16preflight cycles are additional, not included in those totals. Original first5
warmup exclusion and16KiB/.1handle/.02thread-per-cycle thresholds unchanged.
All slopes are below original limits. This is bounded **warm** lifecycle evidence
for the stated cadence, not arbitrary-rate/infinite reconnect or physical playback.

| Target | Persistent consumer memory KiB/cycle | Persistent producer memory KiB/cycle |
|---|---:|---:|
| BGRA 1080p60 | -2.236114 | -2.069709 |
| BGRA 4K30 | -2.243785 | -0.226540 |
| NV12 1080p60 | -2.192665 | -2.282195 |
| NV12 4K30 | -2.397816 | -2.092721 |

Each cell independently rechecked all per-cycle producer/consumer counts, raw
persistent-role slope,126remote SHA256 records/full collection coverage, unchanged
three productionDLL hashes and finalidle0. Fresh-consumer400measured cycles also
have audio.contiguous=yes and queue.accounting=complete; persistent consumer rejects
noncontiguous PCM in its lifecycle result. Controls retain original failures.

- [NV121080](../windows-cpu-lifecycle-nv12-1080/README.md)
- [NV124K](../windows-cpu-lifecycle-nv12-4k/README.md)
- [BGRA1080](../windows-cpu-lifecycle-bgra-1080/README.md)
- [BGRA4K](../windows-cpu-lifecycle-bgra-4k/README.md)

This closes the named shared-CPU repeated-session resource gap only. Native D3D
21.10/84.09/82.528KiB-per-cycle failures remain suspended; native BGRA1080/decoder4K
remain NOT QUALIFIED/excluded. No inferred new device-owner contract. M6open/M7held.
