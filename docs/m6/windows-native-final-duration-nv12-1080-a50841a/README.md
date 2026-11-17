# Native NV12 1080p60 final-candidate duration

PASS: one 1800-second trace-off synthetic native transport run at exact `a50841aad44fbc9431234a4f326bad036d193bf5`. Source, relay and receiver each account for 108000 video frames and 86400000 PCM sample frames. Zero 17ms video/audio arrival misses; maximum final video arrival 7.5106ms, audio 4.1193ms. Memory slopes source/relay/receiver -0.052749/0.112677/0.333879 KB/min; handle/thread gates pass. Full pixel/PCM/timeline/native-lease gates remain unchanged. This does not establish physical presentation, real-decoder performance, repeated device lifetime, or isolated-hop latency.

Remote run completed 2026-11-12T09:07:02Z. The controller was lost before collection and its original `running` receipt is deliberately preserved. Recovery at 11:47Z collected without rerunning media, verified all 43 original manifest entries and coverage, uploads, five unchanged binary pins, profile and phase receipts, final idle, and reanalyzed raw CSVs with the exact source-pinned analyzer. See recovery-receipt.json. EXECUTION_GAP: ready work was idle after remote completion; recovery resumed the final-build NV12 4K short pair.

Original full raw evidence (including CSVs) remains at `<linux-home>/dev/evidence/m6-cpu-path/windows-native-final-duration-prepared/runs/native-nv12-1080-duration-a50841a-20261112t0837z/recovered-data`. This compact archive excludes raw CSVs and tar files. `collected-sha256.json` hashes original external evidence; `archive-sha256.json` hashes copied working-tree bytes (not Git-normalized blobs).

M6 open; API not frozen; M7 held. Prior failures are not waived.
