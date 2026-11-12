# Bounded upstream-production diagnosis — prepared, not executed

Run only after root hands over the Windows lane. No FrameLink source or production changes. Copy this entire directory to a **fresh unique** Windows evidence directory; upload/execute the PS1 rather than an encoded long command:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\execute.ps1 -RuntimeLaneReleased
```

The script refuses an existing `results/`, checks the common mutex and active runtime processes, runs exactly four serial arms (gen-clean, encode-clean, gen-count, encode-count) (90-second owned-process timeout each), saves failure evidence, hashes evidence, and records final lane state. No retries or endurance. Pull the complete directory after completion, including failure logs, then run locally:

```sh
python3 analyze.py /path/to/copied/results > analysis.json
```

## Primary clean pipelines

```text
gst-launch-1.0 -q videotestsrc num-buffers=150 pattern=snow ! video/x-raw,format=NV12,width=3840,height=2160,framerate=30/1 ! fakesink sync=false async=false

gst-launch-1.0 -q videotestsrc num-buffers=150 pattern=snow ! video/x-raw,format=NV12,width=3840,height=2160,framerate=30/1 ! x264enc tune=zerolatency speed-preset=ultrafast key-int-max=30 ! h264parse ! video/x-h264,alignment=au ! fakesink sync=false async=false
```

`videotestsrc` stays non-live (default). Primary mode has no identity element, tracer or per-buffer logs: quiet wall-time measurement only. No pacing, decoder, conversion, hashing oracle, relay, or renderer occurs here. `num-buffers=150` is requested, but clean mode cannot independently validate actual counts/PTS; the report explicitly records this limitation. Process/plugin startup remains included in wall time.

After the two clean arms, the runner executes a separate instrumented pair with `-v`, `identity name=probe silent=false` before fakesink, `GST_TRACERS=stats`, `GST_DEBUG=GST_TRACER:7` and separate trace logs. Instrumented results are count/PTS diagnostics, **not clean throughput measurements**. Do not subtract or transfer their timing into the clean run.

The analyzer verifies hashes for either mode. In instrumented mode it requires 150 identity handoffs and exact normalized 30 fps PTS. If the installed stats-tracer schema identifies exactly 150 outgoing buffers of the named probe, it reports tracer timestamp intervals, never Python log receipt times. Otherwise association is explicitly unavailable. Per-buffer logging can materially stall this arm. No qualification acceptance is inferred in either mode; original 34 ms/full-SHA/PCM gates remain untouched.

Interpretation: generation alone near/above 33.33 ms indicates a synthetic-source ceiling; encoding arm slower than generation implicates the combined encoder/parser stage. Both fast only eliminates a standalone ceiling, not contention/backpressure under the real decode/conversion/oracle workload. Tracing/verbose notification overhead and process startup prevent treating this as isolated codec cost. Pin actual gst-launch, GStreamer and relevant plugin files in status; retain version and exact commands. Existing failing 77afa1a and queue-control evidence remains authoritative for those configurations.

## If still ambiguous — next control, not implemented

Use an explicit optional preencoded-AU tape mode, retaining default on-demand behavior. Prepare the identical snow/NV12/x264/parser profile before measurement; stream each parsed AU into a file record with bytes, exact PTS/DTS/duration/flags and caps, retaining whole-tape and manifest SHA256. Replay records sequentially with bounded `appsrc max-buffers=2 block=true` (disable unrelated byte/time limits explicitly), no all-AU vector and no decoded cache. Keep the actual wrapped-device hardware decoder, conversion, all 150 distinct full-frame SHA matches, exact normalized PTS, PCM, pool2/queue1, pacing and 34 ms gate. This removes concurrent generation/encode cost while preserving actual decode. It is a workload attribution control, not a waiver of the original failure.

## Local controller

`python3 run.py --run-id source-throughput-<unique-id> --runtime-lane-released` uploads a fresh unique directory to <win-user>@<win-host>, invokes the uploaded PS1, collects evidence, verifies every packaged hash and final idle, and runs the analyzer locally. Four arms, no repeat. Do not launch without exclusive runtime handoff.
