# Native BGRA1080 stall/catch-up diagnostic (not qualification)

Isolated exacta50841a archive with **only** `test/fl_gst_local_native_bench_win.cpp` overlaid; no production repository source/binary changes. Full extracted source map and overlay hash verified, four diagnostic binaries pinned before/after each arm. Source pace, CSV insertion, explicit pause and acquire intervals are captured in a pre-reserved300-entry vector, flushed after media completion or on source-main failure. Other steady media logging remains unchanged. Exact failing source acquire return and expected/receivedPTS are printed. Failure-only relay3s grace allows child failure evidence to flush, without synthetic completion/release; finite original watchdog and owned cleanup remain. Diagnostic limited to5s BGRA1080p60. No30min rerun.

## Results

- Trace-off and trace-on controls both pass300video/240000PCM with unchanged17ms/full-payload/PCM/native-lease/trace gates. Source CSV write max0.1192/0.1323ms; acquire max0.0111/0.0063ms. No natural323ms gap reproduced. Initial pace~200ms is intentional pre-epoch wait, not lateness.
- Explicit350ms source pause at identity60 measures350.1415ms. Catch-up source sends identity60 then61 before relay acquires. Relay `ipc.frame_received` sequence62 records replacement(result1); it then expectsPTS1037000000(identity60), gets1053666666(identity61). Source identity62 subsequently returnsFL_TIMEOUT after1999.5518ms. Relay/source traces have zero dropped/rejected events. Receiver process is cleaned by failed relay; do not infer a receiver-side replacement from missing trace.
- Injected executable and original qualification analyzer exit1; **qualification remains FAILED**. Diagnostic phase analyzer succeeds because it verifies the injected cause. Driver `diagnostic-complete` means observation collection, not transport qualification.

This reproduces source-delay→catch-up→default unread queue1 replacement→identity assertion→secondary source timeout. `src/fl_pending.hpp` and native receiver explicitly implement oldest-unread replacement at queue1. No queue/default/pacing-rebase change is justified by this control. Original failure was receiver-sidePTS mismatch after an unobserved-cause323ms pre-acquire gap; this control establishes a mechanism, not identical stage or original pause attribution.

## Jon’s orchestration-interruption alternative

Read-only correlation performed before Windows diagnostic build/run. Execution SSH accepted13:24:11.572Z and disconnected13:35:52.451Z, after remote failedstatus13:35:52.316Z and successful post-run binary/idle checks. Packaging/pull connections follow. No controller timeout/transport error; actual699s termination is below1950s benchmark and2400s SSH bounds. Available Windows logs show no run-window reboot/suspend/kill; process-security auditing has no matching records, so absence is not conclusive. BITS events13:28–13:31 are earlier with no demonstrated causal link. Raw event logs remain private in original external evidence; compact correlation summaries are included.

During339.257ms from original previous video submit to delayed acquire, source audio executes33 actual source timestamps with maximum10.0087ms gap. A whole-process/whole-host suspension for that duration is inconsistent with those observations. Selective video-thread scheduling, CSV I/O, or unlogged host interference remains possible. Do not categorically rule out orchestration-related interference merely because it was not observed.

## Verified evidence and next gate

Collected99original manifest hashes/fullcoverage/uploads, unchanged diagnostic pins, exact overlay and finalidle. Independently reran phase analyzers and both baseline gates locally; injected gate fails as required. Original evidence: `<linux-home>/dev/evidence/m6-cpu-path/windows-native-stall-diagnostic-prepared/runs/native-stall-a50841a-20261112t1353z`. Manifest original-byte domain is distinct from copied pre-Git archive and Gitblob hashes.

Original final BGRA1080 duration remains failed; no clean-repeat waiver. Further natural-stall attribution needs original-scale in-memory phase evidence or independent host/thread/I/O telemetry—not another uninstrumented endurance attempt. The bounded mechanism question is answered; no unsupported production fix. API/default decisionsD2/D3 and historical failures remain open. M6open/M7hold; sole serial executor throughout.
