# Sample reconnect notification race

The Linux backend retires local frame ownership before sending release
notification to a producer. A producer departing between the connected check
and send causes `FL_DISCONNECTED`; the old sample aborted, closed its channel,
and made the replacement producer fail its connection deadline. The consumed
completion API has the analogous locally-recorded-then-notified contract.

The sample now tolerates those two disconnected notification outcomes only.
No ring lifetime, generation, GPU completion, or library synchronization code
was changed. All other sample errors remain fatal. No retry of an already
released frame is performed.

Test-only LD_PRELOAD interposition calls the real APIs first, then returns
`FL_DISCONNECTED` on their first successful result. Each test asserts its
injection marker, so a failed interposition cannot silently pass. This proves
sample outcome handling, not scheduling of a kernel socket failure.

- Old sample: deterministic release test fails at frame zero.
- Old consumed-completion handling: deterministic GPU-sync test fails at frame zero.
- Patched release-only intermediate: Linux 26/26 and 20 repeats each real sample
  reconnect, injection, held-generation tests pass (60 invocations).
- Final combined fix: see `final-suite.log` (27 tests).
- Windows patched consumer: sample tests passed in initial raw-SSH run, but two
  GStreamer executables failed to start without their runtime DLL directory.
  Full suite with the normal GStreamer PATH passed 28/28. Both logs retained.
  POSIX consumed-completion fix does not affect Windows preprocessor branch.
