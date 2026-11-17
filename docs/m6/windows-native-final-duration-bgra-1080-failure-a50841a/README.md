# Final-candidate native BGRA1080p60 duration — FAILED

Exact a50841aad44fbc9431234a4f326bad036d193bf5, intended1800s, aborted around695s. Source failed `acquireResult==FL_OK` (fixture line68), receiver failed expected PTS check (line90). Duration exit1, remote no-next-arm, controller exit1; collection succeeded. All39original hashes/fullcoverage/uploads/five unchanged binary pins verified; exact analyzer fails incomplete capture, as expected. Finalidle13:35:52Z empty. No retry or clean-repeat waiver. Original11d1b65 duration remains separately scoped.

## Observed causal boundary

Source video is contiguous through41714. Frame41713 starts acquire323.211334ms late, but acquire itself takes0.0031ms; frame41714 is307.385467ms late, acquire0.0003ms. Before these, maximum observed pre-acquire lateness is0.4732ms. Previous41712 was on time. Source PCM keeps10ms cadence through the gap. Two subsequent video submits occur less than1ms apart. Thus delay precedes native acquire: prior CSV insertion/file I/O, pacing and OS scheduling remain candidates. Do not claim a proven native acquire stall or storage root cause.

Relay video recorded contiguous through41712. Receiver has41703 complete rows plus one truncated row; abrupt ExitProcess means buffered tail is incomplete, so this is not an exact lost-identity set. Actual failing receivedPTS is not printed. Native default unread queue1 explicitly replaces oldest unread frames; catch-up burst can expose that policy, but exact failure lacks per-frame trace. Full raw timestamps/counts and bounds are in failure-collection-analysis.json.

## Next bounded diagnostic

Use a diagnostic-only copy with in-memory phase records around source pacing and CSV write, and log expected/actual PTS plus native return codes on failure. Preserve original timeline, full pixel/PCM/lease checks, queue1 and17ms gate. Controlled bounded source-delay/catch-up control may establish the drop mechanism separately; do not relabel injected failure as normal qualification. No default/queue/pacing rebase or production fix without causal evidence. Loom skill read; default CLI briefing failed missing chromadb (venv available for follow-up).

Original raw evidence `<linux-home>/dev/evidence/m6-cpu-path/windows-native-final-duration-prepared/runs/native-bgra-1080-duration-a50841a-20261112t1325z/data` retains CSVs; compact archive omits large CSVs/tar. collected-sha256.json hashes original bytes, archive-sha256.json copied pre-Git bytes, Gitblob hashes separate. No other project task or runtime competed. M6open/API not frozen/M7held.
