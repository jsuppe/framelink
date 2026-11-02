# M5b.8 automated validation

Locally validated, not yet milestone-merged.

## What was added

- `test/fl_gst_av_validation.cpp` (ctest `gst_av_validation`), on the public
  elements and coordinator:
  - **Deterministic scheduling.** The consumer pipeline runs on a
    `GstTestClock` with base time 0 and a fixed 10ms latency. All 20
    synchronized render deadlines (10 video, 10 audio, audio +37ms) must equal
    mapped PTS + latency and be released in merged order, and no sink may render
    before its deadline. Nothing depends on wall-clock time. Video pushes are
    gated on the source forwarding the previous frame, because the ring is
    latest-frame by design.
  - **Exact content.** 50 video frames (30x17 RGBA, per-pixel pattern
    compared through the mapped stride) and 49537 audio samples
    (sample-indexed stereo PCM in irregular 1–2399-frame packets) must arrive
    exactly. Every audio buffer's PTS and duration must follow its sample
    index, with no discontinuities.
  - **Offset and drift.** The producer-to-consumer A/V offset error, audio
    PTS-domain drift (span against sample count) and video drift (span against
    frame count) must be exactly 0ns.
  - **Arrival jitter.** Push-to-appsink latency p50/p95/max per track is reported
    in the JSON summary. It is accounting only and never excuses an exact
    mismatch.
  - **Fault-injection controls.** Each validator check must detect its fault:
    corrupt PCM byte, corrupt pixel byte, audio shifted by one sample (A/V
    offset error 20833ns), one video PTS +1ms, a video frame dropped before the
    producer, and an audio buffer dropped after transport. An unannounced
    producer PCM gap is refused before serialization ("PCM sample
    duration/continuity mismatch") and fails the whole session; it does not
    become a silent gap.
  - **Restart rounds.** Ten fresh generations on one coordinator pair and one
    set of elements, rotating clean EOS, consumer death and producer death.
    Each round delivers exact media, and failures surface within 3s.
    - Leak check: the final fd/handle count may not exceed the maximum over
      warm-up rounds 0–2 by more than 4. Windows' process handle count moves by
      about ±10 as GLib and GStreamer worker threads come and go, while Linux
      and Android are exact.
    - Leak control: four unclosed sockets per round must be detected
      (Linux 25→61).
- CTest labels `m5b_av` (10 tests) and `gst_video_regression` (pre-M5b
  video-only element tests: native producer/source, NV12/RGB10A2, timestamps,
  pool policy, sink stats, preconnect, policy generation and the M5a relay) are
  appended in `CMakeLists.txt`.
- `tools/m5b_validation_matrix.py` runs both suites with repeats and writes a
  JSON summary.
  - Desktop: each labelled test runs serially with `--repeat until-fail:N`.
    Every `m5b_av` test must be registered.
  - Android (`--adb`): the static binaries are pushed, and each run gets a fresh
    private TMPDIR that is removed afterwards.
- `test/m5b_gst_av_example.py --adb SERIAL --remote-dir DIR` runs the two-process
  example cases on the device, replacing the earlier ad-hoc Android runner.

## Results

Every run used the final code, serially, with no other suites running.
The first few Linux m5b_av tests overlapped with the tail of a stand-alone
`gst_local_av` diagnostic loop. That only added load, and they passed.

| Gate | Linux x86_64 | Windows (Caspar) | Android arm64 tablet |
|---|---|---|---|
| `m5b_av` suite, x20 each | 10/10 | 10/10 | 10/10 (incl. example via adb) |
| `gst_video_regression`, x20 each | 12/12 | 13/13 | 7/7 |
| Full serial suite | 108/108 | — | — |
| Affected M3/rendezvous tests, x5 | (in full suite) | 5/5 (`m3_av_apps`, `gst_av_session`, `gst_av_faults`, `av_rendezvous`, `av_auth`) | — |
| Builds | — | Release, no warnings | arm64/x86_64 + JNI, no warnings |

One validation run per platform (`*-validation.jsonl`):

| | Linux | Windows | Android |
|---|---|---|---|
| Test-clock deadlines exact / early renders | 20/20, 0 | 20/20, 0 | 20/20, 0 |
| A/V offset error, audio drift, video drift | 0, 0, 0 ns | 0, 0, 0 ns | 0, 0, 0 ns |
| Video arrival p50/p95/max | 1.4/2.6/2.6 ms | 1.7/2.2/2.2 ms | 2.9/4.2/4.3 ms |
| Audio arrival p50/p95/max | 0.24/0.76/1.2 ms | 0.08/0.12/0.22 ms | 2.6/3.4/7.1 ms |
| Restart rounds, handles warm-up max to final | 10, 21→21 | 10, 1581→1576 | 10, 23→23 |
| Worst failure surfacing | 128 ms | 398 ms | 138 ms |
| Fault controls detected | 7/7 + leak | 7/7 + leak | 7/7 + leak |

Matrix summaries: `matrix-{linux,windows,android}-x20.json` and `.log`.

## Findings

- **Rendezvous stall, fixed in shared code.** `gst_local_av` failed about once
  in 150–300 runs on Linux: both roles hit their 2s control-connection deadline
  ("Operation was cancelled"). A gdb capture of the stalled run
  (`hang-326-backtrace.txt`) showed the consumer inside
  `g_socket_client_connect_to_host` → `g_proxy_resolver_get_default` →
  libproxy `px_manager_new` → `g_network_monitor_get_default` → a netlink
  `send()` that cannot be cancelled. That is the first proxy lookup in the
  process.
  - Fix: `g_socket_client_set_enable_proxy(FALSE)` on the private 127.0.0.1
    session connections (`open_local_av`, `open_local_audio`) and in the M3
    sample apps' loopback connect.
  - Deterministic check: with the fix, the test process loads no libproxy,
    libpxbackend, libcurl or duktape (PAC) libraries. The pre-fix control loads
    all of them (`proxy-libraries.txt`). A loopback rendezvous also no longer
    depends on system proxy configuration.
  - The fsync-on-publish suspicion was ruled out by the backtrace.
  - Connection failures now include the underlying GIO error.
- **Windows leak-check false positive.** The first strict per-round handle
  comparison failed on Windows (1564 1563 1554 1557 1560 1560 1561): noise,
  not growth. It was replaced by the bounded rule above.

- The video ring is latest-frame by design. A producer that pushes faster than
  the consumer acquires overwrites unacquired frames, which matches the
  documented pool policy. The test-clock case paces on source forwarding
  instead of wall-clock time. Pipelines with synchronized sinks still need the
  `queue` after each live source that the integration guide requires.
- Under a test clock, a synchronized EOS waits for the last buffer's end, and
  advancing the clock does not release pending waits until each one is
  processed.
- The first leak control was itself invalid: `Pair` closes its sockets
  explicitly, so extra references kept no descriptors open, and the check
  correctly saw no growth. It was replaced by a real unclosed socket. The
  handle count settles at one more after a clean round than after a death
  round on every platform, so rounds are compared with the previous round of
  the same ending. That per-round rule was later replaced (see the Windows
  finding above).
- The device directory held hundreds of stale `framelink-*` and
  `fl-audio-*` directories from earlier runs that used TMPDIR=$PWD. The matrix
  now runs in a scratch TMPDIR. The example runner's adb cleanup had matched its
  own shell with `pkill -f`; it now removes the directory first and uses a
  self-excluding pattern.

## Not claimed

Physical speaker/display synchronization, GPU completion timing and
long-duration clock drift between separate machines remain M6 qualification
work. Arrival jitter here is loopback/IPC jitter on one host.

## Reproduce

```
python3 tools/m5b_validation_matrix.py --build BUILD --repeat 20 --out matrix.json        # Linux
python tools/m5b_validation_matrix.py --build build --config Release --repeat 20 --out m.json  # Windows
python3 tools/m5b_validation_matrix.py --build ANDROID_BUILD --adb SERIAL --repeat 20      # Android
```
