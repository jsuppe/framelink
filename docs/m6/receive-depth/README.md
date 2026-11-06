# Exact negotiated receive depth — 2026-11-06

The local-session helper now rejects a negotiated receive depth that does not
fit its consumer ring before allocating/announcing that channel. The video
source independently enforces this for manually adopted channels. It no longer
silently clamps agreed depth. No default latency or overload policy changed.

Depth1 fits pool>=2. Greater depth requires depth+2<=pool. Queue frames remain
unread capacity, not target fill. The new authenticated setup control accepts
depth1/pool2 and rejects depth2/pool2, with neither peer retaining a channel on
failure. Existing timeline fixtures used depth2/pool2 but actually ran at1 due
to clamping; their declaration is corrected to1 without changing the pool.

Source review: pending queue pops transfer readiness fence ownership; overwritten
or cleared pending POSIX records close the owned descriptor. Consumers serialize
queue operations with pendingMutex. A held frame prevents a second acquire;
reader leases/consumed fences retain retirement protection. Existing regression
coverage includes overflow, shrink, held-frame headroom, disconnect and native
ownership. This is not proof of arbitrary driver/device-loss behavior.

Linux: full114-case suite finished113pass/1fail (old fixture mismatch). After
fixture correction, receive_queue/gst_local_av/gst_av_timeline passed3/3.
No second whole-suite pass is claimed; the fixture-only correction affects the
rerun. Windows isolated MSVC build and same3tests passed. Android API29 arm64
build and all3executables passed on TCL8483A. Evidence retained including failure.
Android x86_64 is not requalified by these tests. No skip treated as a pass.

Next: zero-added-delay sustained target-rate matrix; producer-copy and CPU-memory
gaps remain M6.9/M6.8, not solved by receive queue correctness.

## Subsequent full-suite gates

Linux full114/114 passed at8541985, including the corrected timeline fixture.
Windows full112/112 passed on runtimeb5236a4 after the zero-default migration,
with no failures or skips. Logs are alongside this document. These full suites
do not close the remaining sustained matrix or later M6 tasks.
