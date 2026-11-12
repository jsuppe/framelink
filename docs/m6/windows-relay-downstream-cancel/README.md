# Independent-process downstream cancellation and idle producer failure detection

Base `efb1e5f79ede231fcd4d423f5015ffa370463f43`; status manifests pin each tested overlay.
Owner: Evelyn. Windows-first M6 fault repair, not M6 closure or an API freeze.

## Reproduction and mechanism

The actual final reader cancels its authenticated session after one exact PCM
block while retaining its native texture lease. Source, relay and final reader
are independent processes. Pre-fix, the relay returned TIMEOUT after **2001.999ms**
rather than failure within the preselected **1.5s** bound. The pending lease stayed
owned (correct); failure propagation did not occur (incorrect). The producer's
subsequent acquisition timeout is a consequence, not the root cause.

`FlGstEventChannel::cancel` only cancelled local I/O. Its producer has no reader
thread, so an otherwise idle producer also did not observe peer closure.
Cancellation now shuts down the event socket. Producer state checks and sends
perform a nonblocking socket-condition check: readability means EOF or forbidden
reverse traffic on this strictly one-way event channel. Both fail closed. No new
thread, heartbeat, public symbol, wire format, overload/fallback default or native
lease shortcut. The extra producer readiness check is not a measured throughput
qualification; final performance/lifetime gates remain open.

## Validation

- Windows raw and GstMemory sources: **10.763/11.766ms** observed failure response,
  under the unchanged 1.5s bound. Exactly one video/48 PCM frames/192 PCM bytes;
  original allocation identity, pixels, metadata and 37ms A/V offset verified.
  Further publication rejected; no fabricated END/EOS; finish stays BUSY until
  the actual final reader releases its intact pixels, then native drain succeeds.
  Both source and relay exit0; source cleanup intentionally stops after one frame
  without publishing a successful terminal. This is not automatic upstream
  cancellation propagation or downstream-process-death qualification.
- Windows **9 affected tests passed** in `fix/tests.log`. The tenth, the added
  event-channel check, initially asserted CANCELLED from a wrapper which already
  returns INVALID after get_config latches cancellation. Production code was
  unchanged; the expectation was corrected to preserve that contract. That test
  then **passed 1/1**, `event-wrapper-correction/tests.log`. The prior failed
  assertion is preserved, not counted as a passing initial suite.
- Added portable event tests cover idle producer observation of consumer cancel,
  socket closure and forbidden reverse bytes; no event send is needed to discover
  the failure. Existing quiet-idle, ordering, overflow, partial-record timeout,
  stale-generation and local-cancel controls remain.
- Linux event/session context/session transport **3/3 passed** (3.61s).
- Android same **3 runtime tests passed**, plus arm64 and x86_64 builds.
  Device `A06B4AABFB07983`; no existing Android executor was observed.

Linux's system GStreamer/GLib development headers were missing. The initial
build failure is retained. Sudo was unavailable. Used an unprivileged SDK at
`<linux-home>/dev/build/framelink-host-sdk-20261112`, extracted from Ubuntu dev
packages, relocated pkg-config paths and linked installed runtime libraries.
GStreamer 1.28.2, GLib 2.88.0; the base development package 1.28.2-1 was used with
installed runtime 1.28.2-1ubuntu0.1 after the update-package URL returned404.
No system packages changed. Clean Linux build directory:
`<linux-home>/dev/build/framelink-m6-event-linux-20261112`.

Raw evidence and source tar:
`<linux-home>/dev/evidence/m6-cpu-path/windows-relay-downstream-cancel-20261112`.
The initially reused historical `windows-relay-cancel` evidence directory was
restored from its committed archive; this run's artifacts were moved to the
unique path above. Detail logs retain both named pre-fix and post-fix processes.
Loom briefing unavailable because chromadb is missing.

Next: exact integrated Windows build/API/full suite; then the actual downstream
process-death control, followed by remaining automatic-bin/producer integration.
D2/D3 unresolved, API not frozen, M6 open, M7 maintenance hold unchanged.
