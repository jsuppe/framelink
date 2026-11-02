# M5b.5 partial: ordered video control events

Authenticated, generation-bound role-E stream carries bounded GAP, discontinuity barrier, flush and terminal records. It is separate from native media and has no ordinary per-frame handshake. Queue capacity is 32 records; malformed records, stale generations, overflow and partial-record stalls fail closed. Quiet idle requires no heartbeat.

Video GAP is normalized through its input TIME segment and the common receiver mapping. A discontinuity barrier orders queued GAPs before the next delivered buffer. Tests preserve exact +/-37 ms audio/video offsets across distinct segments. Flush invalidates the receiver session. Mapped video intervals exceeding signed timeline bounds are rejected.

Validation: Linux 9/9; Windows 9/9; Android tablet 5/5. arm64/x86_64 native and JNI builds passed. The initial Android invocation encountered a stale Make target list; explicit CMake regeneration resolved it. These are functional timing/control checks, not physical output or new GPU throughput qualification.

Task 5 remains open: zero-media EOS, final control/lifecycle negatives and review. Event stream integration into forthcoming public A/V bins remains later work. No milestone completion or merge is claimed.
