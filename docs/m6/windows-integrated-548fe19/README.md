# Windows integrated 548fe19 — FAILED, retained

Exact revision: 548fe19b8c11b84758653aafe0df812f92cbc9ab. Clean configure/build and API inspection exit 0. Full serial CTest: **132/133 passed**, exit 8, 586.40 seconds. No acceptance or API-freeze claim.

`m5_external_pipeline` failed the existing `video_late < 10` assertion: BGRA 59 received, 42 scheduled, 17 late, 0 pending-queue overflow; 100 PCM blocks / 96000 samples. This fixture permits skipped video, so 59 received is not the failing assertion. NV12 was not reached. All other tests passed, including coordinated native-producer policy tests.

Original temporary artifacts were recovered from <win-host> `<win-home>/AppData/Local/Temp/fl-m5-pmgabm1b` into `external-negative/`, including media, pipelines and both logs. No rerun or threshold adjustment. `late-analysis.json` joins original source/receive/schedule events by PTS, relative to the logged playback base. First source PTS 0 precedes playback base by 350.240 ms; receive is 1179.2114 ms after base (source-to-receive 1529.4514 ms). Later source events catch up; the 17 late frames span PTS 0 through 1060 ms. The configured 100 ms latency and 40 ms late allowance are unchanged. The trace localizes a startup delay, not its root cause or a proven production regression.

Owner: Evelyn. Next: isolate Windows first-frame native transport/device startup blocking against existing endpoint-ready/startup controls at this exact revision; use a bounded instrumented matched control, not blind clean repeats. Resolve or explicitly suspend this timing defect with evidence before advancing producer A/V-bin policy/allocation wiring and authenticated native-session negotiation. D2/D3 remain unresolved; M6 open, M7 maintenance hold.

Recovery 2026-11-09: local unit inactive/MainPID 0; remote process snapshot shows no fl_*, ctest, cmake, MSBuild or Python executor. No active worker is claimed. Linux BGRA had already been archived and was not repeated. Loom briefing attempted but CLI unavailable in this execution environment.
