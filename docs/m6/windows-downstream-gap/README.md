# Windows downstream gap diagnosis — in progress

Base revision `d8a1f2f177d10e6e1c8df108322d58d38b22fb7a`. Original full-suite **140/142 failure remains unresolved**, not waived by successful controls. API candidate remains8b057a1/not frozen; D2/D3 unresolved, M6open/M7hold.

## Collected unchanged-binary control

At2026-11-12T03:03Z local process inventory and remote Windows inventory were empty. Further correlation of original logs places the delay after the consumer video pad probe timestamp, before sample pull: **46.1217ms at PTS1300000000** in preconnect NV12, **53.9776ms at PTS1060000000** in startup/audio-coordinated BGRA. Original probe records time *before* hash, output-mutex acquisition, printf and fflush. Thus logs cannot distinguish disk/stdio blocking, hashing, scheduling, or downstream appsink delay. Do not claim any of these as root cause.

Bounded matched control changed only child stdout destination: original file versus pipe drained into RAM by a separate harness thread, saved after child exit. Same binaries, all media hashes, producer350ms preconnect delay, native/GST flight recorders, original frame/PCM checks, and independent expected100-PTS set retained. Producer and audio-coordinated clocks, BGRA/NV12: all **8cells100/100**, matching100PCM blocks/96000samples. Probe-to-pull maxima1.8604–2.3529ms. Exit0/pull0, ended03:04:20Z. No original gap reproduced; this does **not** demonstrate a logging fix or qualify a rerun. Binary SHA256 manifest retained with raw evidence.

## Active bounded diagnostic

Diagnostic-only copy of av_apps.cpp adds append-only per-thread in-memory phase records around media hashing, output mutex acquisition, printf, fflush, schedule logging and appsink pull. Existing trace recorder writes at shutdown; no per-event diagnostic I/O. Patch is archived here, **not applied to production source**. Separate Windows tree `<win-home>/dev/framelink-m6-gap-phase-d8a1f2f` from exact base source archive preserves original integrated binaries.

Sole owned executor: `framelink-m6-gap-phase-d8a1f2f-20261112T0305.service`, started03:04:47Z, MainPID655491. Runner builds the sample peers/plugin then runs four bounded paired matrices (32format cells), collecting phase maxima, frame identity gaps, PCM/hash checks and trace overflow footers. These are causal diagnostics, not acceptance retries. Raw status `phase-status.json`, result `phase-analysis.json`, controller `phase-run.py` under `<linux-home>/dev/evidence/m6-cpu-path/windows-downstream-gap-d8a1f2f`.

Next owner Evelyn: collect this executor, inspect every failure and recorder overflow before interpreting phase attribution. If reproduced, isolate the measured phase before repair; if not reproduced, explicitly suspend this intermittent acceptance defect with its evidence/resume condition before moving to automatic two-hop native-bin integration. No blind full-suite rerun, queue expansion, or default-policy change. Loom pre-edit briefing attempted; unavailable because chromadb is missing (also rejected stale --json CLI flag); no inferred requirements.
