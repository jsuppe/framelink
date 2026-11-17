# Native NV12 4K30 layout-cache validation

Exact runtime revision and binary hashes: status.json and run.log. Windows full regression112/112 passed before this untraced1800s run. Pool2/unread1/output1, latency0, transport-only, no-output-preroll; frozen34ms diagnostic and64KB/min memory gates.

54000/54000 video frames and86400000 PCM samples;180000 contiguous audio blocks. Zero video/audio arrival misses, inferred A/V mapping delta0ns. Video arrival p958.495ms/max10.324ms. Consumer privatecommit6.96KB/min (previous69.28); producer45.72KB/min. Both pass64KB/min. CPUconsumer4.66%,producer9.17%; handles/threads slopes effectively flat.

This validates the layout-cache repair for the native NV12 resource case, supported by the matched allocation probe. It does not prove repair of the original rare159ms stall. BGRA native requires its own sustained validation; BGRA CPU remains a separate resource failure. No physical playback or end-to-end zero-copy claim. Raw CSV hashes retained; originals in persistent evidence archive.
