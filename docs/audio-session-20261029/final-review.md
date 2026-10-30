# Independent audio/timestamp review

Reviewed `b498e0b` against master. Timestamp-mode opt-in retains default latency
semantics. Session clock/base agreement, ring versus session generation, fixed
PCM caps, and hardware-clock/Android sandbox limits are documented honestly.
No companion transport is needed for the evidence gathered so far.

Two review improvements:

- Added executable timestamp boundary/segment regression: zero, repeated and
  backward PTS, discontinuity flags, INT64_MAX, missing/overflow rejection,
  nonzero segment start/base and default arrival-time behavior.
- The three-second appsink timeout did not bound blocking appsrc pushes or
  shutdown. Added a portable GLib watchdog to bound the entire sample to 20
  seconds. It exits failure rather than claiming reconnect recovery.

Linux Release full suite passed **30/30**, including both A/V tests and the new
contract test. See `linux-final-review-suite.txt`. Windows/Android results for
this revision must be reported separately.
