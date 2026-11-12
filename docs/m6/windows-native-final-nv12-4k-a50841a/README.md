# Final-build native NV12 4K30 short pair

PASS at exact a50841aad44fbc9431234a4f326bad036d193bf5. Five-second trace-off and trace-on arms each account for150video/240000PCM per role; zero34ms video/audio arrival misses; maximum final arrival5.534367ms and9.271867ms respectively. Full trace join passes. Five binaries unchanged and DLLs match integrated candidate. 64 original collected hashes/full coverage verified. Exact source-pinned analyzer independently reran locally. Controller completed11:48:05Z on2026-11-12 with all phases0 and final Windows idle. No build, retries, or gate changes.

Synthetic authenticated transport with explicit upload/readback validation, not decoder or physical presentation. Short pair permits one1800s duration; it does not itself qualify resource endurance. M6 open/API not frozen/M7 held.

External original evidence: `<linux-home>/dev/evidence/m6-cpu-path/windows-native-final-target-prepared/runs/native-nv12-4k-a50841a-20261112t1148z`. `collected-sha256.json` records original bytes; `archive-sha256.json` records copied pre-Git-normalization bytes.
