# M5b.6 lifecycle and security: closure

Task6 is locally validated within the scope below. Each requirement maps to a
test that runs on Linux, Windows and the Android tablet unless noted.

| Requirement | Evidence |
|---|---|
| Both startup orders | Standalone: `gst_audio_source`, `gst_local_audio`. Coordinated A/V: reconnect generation1 consumer-first, generation2 producer-first (new). |
| Coordinated pause/resume | [pause/context](../lifecycle-pause/README.md), [in-flight pause](../inflight-pause/README.md), [rapid audio pause](../rapid-audio-pause/README.md) |
| Track loss and cancellation | [fatal-error propagation](../fatal-propagation/README.md): audio transport loss, element and audio-bin child errors cancel the shared session |
| Peer death and reconnect | Consumer death in [fatal propagation](../fatal-propagation/README.md); same-coordinator generation2 in [reconnect](../reconnect/README.md) |
| Stale generations | Generation1 mapping, readiness, wait, mapping lookup, stream claim and reconfigure all STALE after replacement; stale authentication in `gst_session_transport` |
| Negotiation rejection | Track-set mismatch in both directions for both audio elements (new); malformed context; wrong key, offer mismatch and invalid config in `gst_session_transport` |
| Bounded cleanup | Cancellation observed within 100ms; permissive-directory rejection within 2s; sink backpressure teardown in `gst_audio_sink` |
| Endpoint permissions and authentication | Permissive session directory refused by both standalone roles on Linux/Android (new); Windows NULL-DACL rejection in `av_rendezvous`; missing authentication in `gst_audio_sink` |

New negative controls in this closure:

- `permissions-negative.log`: with the rendezvous owner-only mode check
  disabled, producer and consumer connect through a 0750 directory and the
  test fails. An earlier version of this test passed under the same control
  because each role failed alone for an unrelated reason; it was rewritten so
  both roles run together and the producer must report the permission error.
- `tracks-negative.log`: with both audio elements' track comparison disabled,
  the mismatch check fails at the first element.

Final results:

| Platform | Result |
|---|---|
| Linux full suite, serial | 105/105 (`linux-serial.log`) |
| Linux timeline and local-audio repeat | 20/20 |
| Windows affected suite including `av_rendezvous` | 10/10 |
| Windows timeline and local-audio repeat | 20/20 each |
| Android tablet timeline and local-audio repeat | 20/20 each |
| Android audio sink and source | 1/1 each |
| Android arm64/x86_64 and JNI builds | passed, no warnings |

Limits: reconnect requires the application to stop both pipelines and install
a strictly newer generation; elements do not reconnect automatically. The
POSIX permission check is not run on Windows, where DACL coverage comes from
`av_rendezvous`. Android signed cross-UID ownership for the public audio
elements relies on the M5.3 broker path and is requalified in task12. No
physical output claim.
