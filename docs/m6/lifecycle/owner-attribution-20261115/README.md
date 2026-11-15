# Option B restart-consumer growth: attribution (2026-11-15)

This is a diagnostic, **not qualification**. It doesn't change the gate or waive anything. Host <win-host>, RTX 5090, same driver as 11-13. Runs were serialized under `Local\FrameLinkFocusedWindowsValidation`. framelink.dll sha256 `50c56169…f9ff9` is the exact binary from the 11-13 run (`../device-owner-20261113/matched-100/binary-pins.json`).

## 1. FrameLink only, no GStreamer (`owner_probe.cpp`, 60 cycles × 300 frames, 60 fps)
The long-lived producer holds a device owner. Each cycle a fresh consumer process (BGRA 1920x1080, pool 2) receives 5 s of frames. Heap = live HeapWalk bytes.

| Arm | Private KiB/cycle | Heap KiB/cycle | After owner release |
|---|---:|---:|---|
| map (flImageMap write + submit) | 35.05 | 35.56 | heap back to 457 KiB |
| submit only (no map) | −1.83 | −0.31 | — |
| attach only, no frames | −1.08 | −0.26 | — |
| none (no FrameLink) | 0.05 | 0.01 | — |

All the growth is in live heap, it's tied to the CPU map path, and it's released together with the retained device. Within the map arm the slope falls from 47 to 44 to 5 over successive 20-cycle windows.

## 2. Plain D3D11, no FrameLink (`d3d_probe.cpp`, persistent hardware device, 2 textures, 150 Map rounds/cycle)
| Variant | Heap KiB/cycle |
|---|---:|
| create/release staging only; + Map/Unmap; open shared only | 0.00 |
| 1 copy round per cycle | 0.00 |
| CopyResource DEFAULT↔STAGING, fresh textures (local or NT-shared) | 32 |
| download only / upload only | 27 / 28 |
| staging kept / DEFAULT kept | 32 / 32 |
| **both kept for the whole run (no release at all)** | **32** |
| UpdateSubresource (no staging) | 15 |
| **400 cycles, fresh textures** | cycles 0–49: 31.4; 50–99: 5.1; **100–399: 0.00** (plateau about 1.9 MiB above the start) |

## Conclusion
- The remaining restart-consumer slope comes from copy volume, not lifecycle. The driver/runtime grows a bounded internal heap pool on a long-lived device as copy work accumulates. Connection churn isn't needed (the "both kept" variant grows the same), and nothing grows once the pool has warmed up.
- The production case does about 600 copies per cycle (300 frames × readback + upload), the same as the probe.
- The 11-13 bench series rises over cycles 20–79 and tapers in cycles 80–99 (20-cycle slopes 0.3 / 20.0 / 34.6 / 24.7 / 10.4). So a 100-cycle window with warmup 5 measures this warm-up.
- This explains the result; it doesn't pass the gate. The matched 100-cycle criterion still fails as defined. Any change to the warmup or window is Jon's decision. A longer production run (for example 200 cycles) would directly show whether the real pipeline plateaus.
