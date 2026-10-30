# M4 live playback recovery checkpoint

`PlaybackOptions.paused` suspends the entire live session. It does **not** retain
an unbounded paused-media backlog: audio stops and native video receives `onEnd`;
resume performs fresh authentication/configuration and one shared-clock epoch.
Producer sample must accept reconnections (`--sessions 2` or restart it); completed
session counts do not include suspended sessions. Lossless recorded-media pause is
not implemented by this policy.

`recoveryAttempts` defaults to 3, bounded to 16. Pipeline errors, clock-loss events,
and audio continuity violations terminate both tracks and retry a new session.
Invalid PCM buffers are discarded before reaching the playback sink. Each attempt
retains existing connection/playback timeouts; an intentional pause waits
cancellably without consuming attempts. Terminal failure returns the error.
Callbacks report `session-paused`, `session-reconnecting: reason`, and clock choice.
Native consumer must release/retire the old ring on `onEnd`, before accepting its
new `onSession`/`onClock` pair. Device/driver teardown latency is platform-dependent.

A producer-side sink failure watcher stops the failed pipeline from a separate
thread, releasing appsrc backpressure; previously a disconnected audio consumer
could strand the producer and prevent any replacement session.

Audio continuity allows an intentional nonzero initial producer PTS; samples remain
unaltered. The first PTS is a validation origin, not a first-arrival clock anchor.
Drain includes the audio endpoint. Without a declared starting sample position,
this does not distinguish initial missing samples from intentional initial offset.

## Evidence

Linux 39/39 regression tests passed before the final origin/end-notification edits.
After those edits, `av_offer_bounds` and `m4_av_recovery` exercise:

- malformed outgoing offer bounds before any socket writes;
- a real `GST_MESSAGE_CLOCK_LOST` invokes joint-session failure policy;
- 37 ms initial audio PTS remains untouched and continuity is accepted;
- bounded no-peer retry failure;
- ordinary 75-block authenticated playback;
- pause and fresh-session resume, two clock callbacks;
- corrupt-audio-PTS session followed by a healthy replacement, two sessions.

The recovery fixture uses a fake audio sink and actual FrameLink transport, not
physical output. Clock-loss event policy is tested; actual device unplug/reopen is
not yet qualified. Audio sink RESAMPLE slaving is configured, but long-duration
producer-vs-device drift compensation remains an explicit unqualified task.
