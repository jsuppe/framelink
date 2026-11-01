# Startup deadline versus steady-state socket idle timeout

The Windows startup regression failed before `startup-released`, in fresh
processes. The stale-caps correction is unrelated. Code inspection found all
control and audio sockets limited to a two-second idle read even while device
startup was explicitly allowed four seconds.

A deterministic Linux reproduction with 2300ms injected preroll and 4000ms
configured timeout produced the same two control read/disconnect errors. Giving
only control sockets the startup budget advanced further but exposed the same
idle timeout in the audio socket: the first PCM priming packet arrived, followed
by a >2s gated gap, and the audio reader prematurely exhausted. That intermediate
control-only failure is preserved here rather than hidden.

Fix: during mode2 coordinated startup, both control peers and the consumer audio
socket use the existing configured timeout. Restore the ordinary two-second idle
timeout immediately at GO. Authentication/default playback are unchanged. No
media timestamp, latency, or wire-format change. The executable's whole-process
watchdog and the library's bounded stage waits remain in force. Reads now retain
GError detail so timeout and peer closure are distinguishable. Fault injection
reports actual elapsed time, not just its requested delay.

Linux final pair: 2300ms request actually 2482792us; exits 0/0 in 3.62s,
40/40 video scheduled, 38400 audio samples, zero late/overflow, 100ms latency.

`m5_startup.py` now retains original cancellation/restart scenarios and adds a
>2s preroll case with a six-second process budget (allowing platform sleep
overshoot and media playback). It asserts complete video/audio, not just exits.
Original Windows failed logs remain with the Windows validation owner; this
Linux reproduction does not itself qualify that platform. Windows rerun needed.
