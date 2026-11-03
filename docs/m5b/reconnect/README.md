# Task6 partial: reconnect on a fresh generation

New `gst_av_timeline` reconnect case, run on the same coordinator objects:

1. Generation1 delivers three A/V frames.
2. The consumer dies and its audio socket closes. The producer writer fails,
   the producer session cancels, and the producer pipeline stops.
3. Both sides configure generation2. Generation1 operations now return STALE:
   mapping install, track readiness, readiness wait, running-time mapping,
   audio-stream claim, and re-configuring generation1.
4. A fresh authenticated audio stream, a different key, a fresh video lease
   and a different receiver mapping (anchor 200ms instead of 100ms) are installed.
5. Both pipelines restart. Six generation2 frames must carry exact pixels, PCM,
   the generation2 mapping, the +37ms audio offset and video metadata
   `streamGeneration==2`, then reach clean EOS.

Defect found: `framelinkaudiosrc` registered its track with the coordinator on
every NULL->READY. The coordinator retains registrations across generations,
so the restarted consumer failed with "invalid or duplicate consumer audio
context" (`reconnect-before-fix.log`). The source now registers once per
installed coordinator, like the audio sink and video elements, and resets that
when a different session context is installed. A second distinct audio source
on the same coordinator is still rejected as a duplicate.

Results on the final source:

| Platform | Result |
|---|---|
| Linux full suite, serial | 105/105 (`linux-serial.log`) |
| Linux affected suite / timeline repeat | 8/8, 20/20 |
| Windows (Caspar) affected suite / timeline repeat | 8/8, 20/20 with reconnect each run |
| Android tablet timeline repeat | 20/20 with reconnect each run |
| Android audio sink and source | 1/1 each |
| Android arm64/x86_64 and JNI builds | passed, no warnings |

Stale-generation authentication of an old stream is covered separately by
`gst_session_transport`. Reconnect requires the application to stop both
pipelines and install a new generation; automatic reconnection inside the
elements is not provided or claimed. No physical output claim.
