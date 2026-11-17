# Windows startup phase attribution — diagnostic, not acceptance

Base source `461584bbf5a6b8b70cec1578d69d35cb4b1ea2ff` with the adjacent **diagnostic-only** patch, not a production change or API candidate. Windows11 build26300, RTX5090 driver32.0.16.1714. Exact source/binary hashes, host metadata, scripts, raw timing/media rows and machine-readable analysis accompany this report.

The original **1529.4514ms** failure measures source-to-receive latency, not a proven producer-open duration. The original failure remains preserved and suspended. These two serial arms did not reproduce it.

## Observations

| Profile | Format | Source / received / scheduled video | PCM sample frames | Late video | Open total / device creation (ms) |
| --- | --- | --- | --- | --- | --- |
| Legacy audio | BGRA | 100 /94 /94 | 96000 | 0 | 164.1182 /163.5418 |
| Legacy audio | NV12 | 100 /94 /94 | 96000 | 0 | 160.9698 /160.3623 |
| Coordinated audio | BGRA | 100 /100 /100 | 96000 | 0 | 157.9971 /157.3722 |
| Coordinated audio | NV12 | 100 /100 /100 | 96000 | 0 | 158.8667 /158.2891 |

Both commands exit0. Independent raw accounting checks exact100 source video/audio PTS identities, all received hashes/lengths/durations,100 contiguous PCM blocks/384000 bytes per case, terminal counts, phase completeness and trace overflow. No missing or extra audio, no trace drops. The legacy fixture permits video loss: its success is **not lossless acceptance**. Missing video PTS(ns):

- BGRA:0,40000000,60000000,80000000,120000000,140000000.
- NV12:0,20000000,40000000,100000000,120000000,140000000.

Legacy PTS0 is absent at the receiver; its original-style first-frame latency therefore cannot be calculated. Maxima among received identities were23.0732ms BGRA /23.5404ms NV12; these exclude missing frames and must not replace the historical metric. Coordinated PTS0 source-to-receive was51.9786/54.5492ms. This diagnostic does not qualify the primary17ms target.

Device creation accounts for nearly all measured open time; measured thread CPU was140.625–156.25ms, with coarse Windows thread-CPU resolution. Imports took0.441–0.501ms; connect/Hello/ring waits were small. Legacy first-source-video to open entry already adds approximately263/326ms, so the open cost is not total startup. This supports device creation as the dominant **current** open cost, not attribution of the historical long delay to a driver or OS scheduling. No scheduler cause is established. The timestamp bridge exists in every successful record (observed offset variation at most600ns). Automated nearest-format association is explicitly heuristic; independent review additionally matched producer-log PID to native/phase PID and FNV of the actual session channel to native `flOpenProducer.channel_id` in all four cases. The existing producer log display appends a literal0 when slots are disabled; it must not be hashed verbatim. This minor diagnostic-log defect does not explain loss.

## Execution and limits

- Fixed-capacity in-memory stamps; output only at normal DLL teardown. No per-phase file I/O, sleep, timeout or production policy change. Instrumentation still adds timer-call overhead.
- Initial launch failed Windows command-line length before executing the script. Uploaded-script launch built successfully, then its strict pre-test check caught a transient MSBuild node-reuse worker. No media arm ran in either failure. The worker was independently observed gone; no process was killed.
- Resumed the same four hashed runtime binaries without rebuilding. Both media arms ran once, serially. Failed-launch logs remain under `pretest-failures/`; the reproduction runner now uploads long scripts and disables MSBuild node reuse.
- Raw source archives, full diagnostic source, decoded AVI and local discovery files remain outside git at `<linux-home>/dev/evidence/m6-cpu-path/windows-startup-phase-461584b/`. No discovery credentials are copied here. The reviewed patch and manifests identify the exact diagnostic source.

**Result:** no evidenced compatible production fix for the historical1529ms event follows from this run. Evelyn owns startup attribution; resume on original-scale evidence with these phase boundaries (and targeted wait-stack/scheduler evidence if needed), not equivalent clean repeats. Default-profile21.10/84.09KiB-per-cycle resource failures likewise remain suspended under the existing device-lifetime contract. D2/D3 remain Jon-owned. Continue other ready Windows M6 qualification; API not frozen, M6 open, M7 held.
