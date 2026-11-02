# Task6 partial: coordinated fatal-error propagation

A fatal error in any one public element now cancels the shared session, so the
sibling track fails promptly instead of waiting for its own idle deadline.
Flushing and EOS flow returns are not treated as fatal.

- Video sink render/event, video source create and the audio reader return path
  cancel the session on fatal flow results or a rejected event.
- Both audio bins (`framelinkaudiosink`, `framelinkaudiosrc`) now cancel the
  session when any inner child posts an error: GDP payloader/depayloader, bounded
  queue or GIO socket writer. The error message still reaches the pipeline.

New `gst_av_timeline` cases:

- Fatal-error cases (missing mapping, overflowing mapping, unsupported rate) now
  also require the failing side's session to reach CANCELLED within 100ms.
- Consumer death: receiver pipeline stops and its audio socket closes. The
  producer GIO writer fails ("Could not write to resource") and the producer
  session must cancel within 100ms.
- Audio transport loss: only the producer audio connection closes while video
  stays alive. The consumer reader reports "audio peer closed before GDP EOS" and
  the receiver session must cancel.

Negative controls (logs retained):

- `before-fix.log`: with the element-level plugin change reverted, the first
  fatal-error case fails at the cancellation check (line69).
- `sink-handler-negative.log`: with only the audio sink child-error handler
  disabled, the consumer-death case fails at the cancellation check (line81).
- `src-handler-negative.log`: disabling both consumer-side audio changes does
  NOT fail the transport-loss case. GstBaseSrc forwards EOS after the reader
  error and the existing missing-terminal-accounting guard already cancels. This
  case is therefore a regression guard for existing behavior, not proof of the
  new source handler. The source handler covers depayloader errors, which the
  bounded wire pre-check normally prevents; no direct fixture exists for it.

Results on the final source:

| Platform | Result |
|---|---|
| Linux full suite, serial | 105/105 (`linux-serial.log`) |
| Linux `gst_av_timeline`, fatal warnings | 20/20 |
| Windows (Caspar) affected suite | 8/8 |
| Windows `gst_av_timeline` repeat | 20/20, both death cases every run |
| Android tablet `fl_gst_av_timeline` | 20/20, both death cases every run |
| Android audio sink and source | 1/1 each |
| Android arm64/x86_64 and JNI builds | passed, no warnings |

A parallel Linux `ctest -j8` run failed six M5/broker tests that share fixed
endpoints; all six passed when rerun serially and in the full serial run.

Task6 remains open: reconnect with a fresh authenticated generation, rejection
of stale payload/control messages after reconnect, and remaining
security/lifetime gates. No physical output or automatic recovery claim.
