# Prepared startup phase attribution — NOT RUN

## Executable artifacts

After the coordinator releases the Windows lane, execute `python3 run.py --runtime-lane-released` from this artifact directory. It creates a unique `runs/<UTC>-<nonce>` directory, archives and verifies the exact git object, verifies the uploaded archive and diagnostic source on Windows, builds only the three required targets, and runs the two arms serially under the existing named lane mutex. It retains failures and pulls remote `results/`; it never terminates other processes or retries either arm. SSH timeouts leave `remote_may_still_run=true`: inspect that existing remote run, do not launch another.

Run `python3 analyze.py runs/<id>/results` to regenerate independent accounting. Neither script acts on import. `run.py` returns nonzero for build/transport/analysis failure or a fixture's nonzero exit; all raw evidence remains available.

The patch also records an optional `gst_ns` bridge by calling the **already loaded** GStreamer module's exported `gst_util_get_timestamp`, the same function used by `media-trace`. It never loads a module or adds a link dependency. `steady_ns` calls the existing `fltrace::clockNs()` from `include/framelink_trace.hpp`, precisely matching native trace timestamps; GStreamer traces instantiate that same header as `gsttrace`. If the GStreamer export is unavailable, `gst_ns=0` and the analyzer refuses to assume cross-domain epoch equality. Format association by nearest first-source timestamp is explicitly heuristic; raw timing gaps and process IDs are retained independently.

Owner: root coordinator. Run only after the integrated/API compatibility lane is collected and explicitly released. No Windows command has been run for this artifact. No repository source was changed.

## Exact question and limits

Base: `461584bbf5a6b8b70cec1578d69d35cb4b1ea2ff`.

Correction to the shorthand “1529 ms producer open”: the original evidence establishes **1529.4514 ms first-source-to-receive latency**, not a measured 1529 ms `flOpenProducer` call. Original BGRA counts were 100 source frames, 59 received, 42 scheduled, 17 late; audio remained 100 blocks/96000 sample frames. See repository `docs/m6/windows-integrated-548fe19/README.md` and `external-negative/`. The original AVI, pipeline and logs survive; original CPU scheduling/driver state does not. Current-revision results are diagnostic comparisons, not exact historical reproduction.

Hypothesis: lazy legacy producer opening accounts for a substantial part of first-frame delay. Isolate connect, Hello receive/send, device creation, device-context setup, RingDesc receive and ring import. Compare legacy `audio` and preconnected `audio-coordinated`, with all other fixture options unchanged. Do not inject another 1200 ms delay: the prior injection already established that coordinated startup places setup before playback. Do not change thresholds, retry a failed arm, run a full suite, or claim a clean run resolves the historical failure.

## Diagnostic patch

`startup-phases.patch` changes only `src/framelink_win.cpp`; `framelink_win.diagnostic.cpp` is its full reviewable result. `prepare.py` regenerates both from the exact git object without modifying a checkout. `manifest.json` records source and patch hashes.

When `FRAMELINK_SETUP_PHASE_DIR` is set before loading the DLL, the patch records **legacy** open calls only. Each process has fixed storage for 64 opens × 24 phase marks, containing QPC wall ticks, the existing native trace's steady-clock nanoseconds, thread kernel/user CPU times, PID/TID, success and terminal-return mark. No per-phase file output, lock, heap allocation, sleeping or timeout changes. Failed calls retain partial marks and a terminal mark.

CSV output is deferred until normal DLL/CRT teardown, after measured media work; there is no diagnostic thread. Dumping uses ordinary stdio only (no waiting or GPU work), and is diagnostic-only, not a production DLL-lifecycle proposal. A forcibly terminated process may produce no file; that is missing evidence, not a fast open. Reject truncated/incomplete marks and attempts greater than capacity. The timer/system calls still perturb execution; report instrumentation overhead as a limitation. Failure cleanup is included between its final phase mark and `return`, not attributed to the interrupted phase automatically.

## Isolated preparation (root executes later)

Use a fresh, non-production Windows checkout/worktree at the exact base. Never patch the ongoing integrated build. Keep pristine base and diagnostic binary hashes in the result directory. Confirm no existing `fl_*`, `framelink*`, `ctest`, `cmake`, `MSBuild`, or prior fixture executor is running before taking the lane; retain the existing coordinator ownership protocol.

Commands below assume working directory `<win-home>/dev/framelink-startup-phase-461584b`, artifact copies in `<win-home>/dev/startup-phase-artifacts-461584b`, and an absent result directory `<win-home>/dev/startup-phase-results-461584b`. These are proposed paths, not existing resources.

```powershell
$ErrorActionPreference = 'Stop'
if ((git rev-parse HEAD).Trim() -ne '461584bbf5a6b8b70cec1578d69d35cb4b1ea2ff') { throw 'Wrong base' }
if (@(git status --porcelain).Count) { throw 'Base is dirty' }
git apply --check <win-home>/dev/startup-phase-artifacts-461584b/startup-phases.patch
if ($LASTEXITCODE) { throw 'Patch check failed' }
git apply <win-home>/dev/startup-phase-artifacts-461584b/startup-phases.patch
if ($LASTEXITCODE) { throw 'Patch failed' }
$env:PATH = 'C:/Program Files/gstreamer/1.0/msvc_x86_64/bin;' + $env:PATH
cmake -S . -B build-phase -G 'Visual Studio 17 2022' -DFRAMELINK_BUILD_TESTS=ON -DFRAMELINK_BUILD_GST=ON -DFRAMELINK_BUILD_SAMPLES=ON
if ($LASTEXITCODE) { throw 'Configure failed' }
cmake --build build-phase --config Release --target framelink_av_producer framelink_av_consumer gstframelink
if ($LASTEXITCODE) { throw 'Build failed' }
```

Capture configure/build logs, source patch, `git diff`, compiler/CMake/GStreamer/adapter/driver versions, active-process snapshot and SHA256 of `framelink.dll`, `gstframelink.dll`, both executables, and fixture scripts before running. Check the patched source hash against `manifest.json`. Do not use this dirty diagnostic revision as an API candidate.

## Minimal matched run: two commands, serial

Use the same diagnostic DLL and fixture for both arms; fresh processes per arm, BGRA then NV12 as the unmodified script selects. Clear inherited fault-injection settings and trace knobs before setting these explicit ones. Capture the complete inherited `FRAMELINK_*` configuration without credentials and abort if an unknown fault setting remains.

```powershell
$resultRoot = '<win-home>/dev/startup-phase-results-461584b'
if (Test-Path $resultRoot) { throw 'Preserve existing evidence' }
New-Item -ItemType Directory $resultRoot | Out-Null
Remove-Item Env:FRAMELINK_TEST_PRODUCER_OPEN_DELAY_MS -ErrorAction SilentlyContinue
Remove-Item Env:FRAMELINK_TEST_LAZY_PRODUCER_OPEN -ErrorAction SilentlyContinue
$env:PATH = (Resolve-Path build-phase/Release).Path + ';' + $env:PATH
$env:GST_DEBUG = 'framelink:4'

$arm = New-Item -ItemType Directory "$resultRoot/audio"
$env:FRAMELINK_SETUP_PHASE_DIR = $arm.FullName
$env:FRAMELINK_TRACE = "$($arm.FullName)/native.csv"
$env:FRAMELINK_GST_TRACE = "$($arm.FullName)/gst.csv"
python test/m5_external_pipeline.py --build build-phase/Release --output $arm.FullName --audio-sink fakesink --clock-policy audio *> "$resultRoot/audio-command.log"
$legacyExit = $LASTEXITCODE

# Record legacyExit and preserve partial files even on failure; do not retry.
$arm = New-Item -ItemType Directory "$resultRoot/audio-coordinated"
$env:FRAMELINK_SETUP_PHASE_DIR = $arm.FullName
$env:FRAMELINK_TRACE = "$($arm.FullName)/native.csv"
$env:FRAMELINK_GST_TRACE = "$($arm.FullName)/gst.csv"
python test/m5_external_pipeline.py --build build-phase/Release --output $arm.FullName --audio-sink fakesink --clock-policy audio-coordinated *> "$resultRoot/audio-coordinated-command.log"
$coordinatedExit = $LASTEXITCODE
@{ legacyExit=$legacyExit; coordinatedExit=$coordinatedExit; scope='diagnostic only' } | ConvertTo-Json | Set-Content "$resultRoot/exits.json"
```

Before the second command, confirm the first command's children have exited. If the first process hangs beyond the script's 30 s media-generation/25 s peer limits, use the coordinator's bounded command timeout, preserve evidence and inspect lingering actors before proceeding. This plan never authorizes overlapping arms. A first-format assertion stops that arm before NV12; record NV12 as not executed, not passed.

## Independent evidence/count gates

Do not rely only on `summary.json` or script exit status (the legacy fixture deliberately permits video loss):

1. Read raw JSON `media-trace` rows in each producer/consumer log. Require no duplicate `(track, pts_ns)` keys before constructing maps. Expected source video and audio PTS sets are exactly `{n * 20000000: n=0..99}`. Source video count=100; source audio count=100, duration=20000000 ns, 3840 bytes/block, total=384000 bytes=96000 stereo S16 sample frames.
2. Audio receive set, bytes, SHA256 and duration must exactly equal the source; no missing/extra blocks. Compare every received video PTS/hash/bytes/duration to the source, list missing source identities explicitly, and reject extras/duplicates. No dictionary collapse may hide duplicates.
3. Join `playback-complete` and scheduling rows to actual receiver/source identities. For the coordinated control, require all 100 video frames received/scheduled and zero late frames as the desired diagnostic control. For legacy, report exact loss/late counts; retain the existing script gate `video_late < 10`, but never call its success lossless. Always require `audio_samples=96000`. Unexpected source count, audio mismatch or absent terminal means incomplete/failed evidence, not a setup-performance result.
4. Each reached producer must have a successful legacy-open phase record with `enter` and `return`, all expected phase pairs, monotonically increasing QPC/steady/CPU values where valid, and attempts≤64. Missing CPU measurements prohibit CPU attribution but not wall-time reporting. If no phase record appears, inspect DLL selection or abnormal termination; never report zero duration.
5. For each interval, wall_ms=`1000*(end.qpc-begin.qpc)/qpc_hz`; CPU_ms=`((end.kernel+end.user)-(begin.kernel+begin.user))/10000`. Report uncovered inter-phase/cleanup time and total. Join steady_ns with native/GStreamer timestamps and first source/receive records to test whether open really overlaps and explains the first-frame gap.
6. CPU deficit is not proof of OS descheduling: waits inside driver/IPC calls also consume little thread CPU. A reproduced long phase warrants one specifically targeted scheduler/wait-stack capture or deeper phase split. If the long historical gap is absent, report non-reproduction and retain the suspension. No automatic follow-on campaign.

The source pattern, media dimensions/rate, decode chain, clock choices, fakesink and historical thresholds are available. Original scheduler state and precise historical driver-cache state are not. Neither this diagnostic nor its coordinated control qualifies physical playback, lifecycle memory slopes, API freeze or M6 completion.
