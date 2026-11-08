# Shared CPU A/V terminal coordination prerequisite

Tested on parent revision 0bd7864 plus the accompanying session-context tests.
Linux three affected tests pass; Android three runtime binaries pass; arm64 and
x86_64 builds pass. Commands, logs and exact source hashes: av-terminal-prerequisite/.

The coordinator accepts identical terminal observations from either video or
audio accounting, rejects conflicting frame counts/last PTS without overwriting
the first observation, distinguishes empty streams (0,-1), and clears terminal
state on epoch replacement. Waiters observe replacement as STALE and cancellation
as CANCELLED; failed waits preserve outputs. Both empty and nonempty cases pass.
No production change was required. This does not identify the originating track:
the two callers use the same API. Caller-level cancellation remains required on
contradictory observations (audio source already does this).

This is NOT integrated PCM/video qualification. CPU elements still deliberately
reject audioTrackId. Before removing that guard, exercise actual independent audio
and CPU video streams with authenticated event controls, common clock mapping,
terminal arrival in either order, contradictory accounting, peer death and bounded
cancellation. Require aggregate EOS only after both streams finish. Then wire the
explicit selection through A/V bins and authenticated rendezvous; no implicit
fallback or native-channel casting. Windows allocator and rate/endurance gates
remain open. M6 remains incomplete; hold before M7 unchanged.
