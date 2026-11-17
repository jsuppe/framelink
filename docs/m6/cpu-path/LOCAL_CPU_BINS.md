# Explicit local shared-CPU bins

Linux/Android framelinkavsink and framelinkavsrc now accept shared-cpu=true
with session= in a protected current-user directory. Their existing cancellable
workers use the authenticated Unix CPU setup helper. The producer preserves
the bin-selected clock epoch. Media release claims the coordinator CPU channel
instead of trying native adopt-channel. Startup readiness and terminal drain
remain unchanged. Native defaults are unchanged, there is no native/TCP fallback,
and audio-only or native-memory combinations remain invalid. Windows support was subsequently implemented and validated in
[Windows local bins](windows-local-cpu-bins/README.md). Android cross-UID clients still need an
authorized broker; this mode is current-user only.

Functional evidence:
- Twenty integrated A/V cases: twelve broker-bound, four explicit local-helper,
  four direct session= bin cases (BGRA/NV12, populated and empty). Exact pixels,
  PCM,37ms relative A/V offset and EOS verified. Direct-bin absolute PTS is not
  independently calibrated: frame-to-frame progression and A/V difference are.
- Three invalid-bin settings, five helper negotiation/deadline/cancel negatives,
  and producer/consumer bin teardown under1s despite a60s discovery deadline.
- Linux5/5 and Android4 runtime binaries plus both ABI builds; Windows4/4
  native-bin/auth/discovery/transport compatibility. Android retains the two
  documented unavailable hard-link/FIFO fixture cases, not hidden passes.
- Two independent Linux gst-launch processes, no inherited session context:
  each format delivers32 exact64x64 video frames and25920 stereo PCM frames.
  Receiver files exactly match independent producer tee reference files, including
  complete lengths and SHA256. Both processes exit0. This is a small correctness
  smoke, not target-resolution throughput, endurance or latency qualification.

The initial test build omitted initializer_list; fixed before final validation.
A newline-following ctest during that failed build ran the stale binary and is
explicitly excluded. Fresh build/test logs, hashes and scripts are preserved in
local-cpu-bins/. No production failure was hidden by that stale pass.

M6.8b/c remain open: Windows shared-CPU support and full-resolution rate/resource/
copy-accounting gates still pending. M6.9 native producer work is separate.
