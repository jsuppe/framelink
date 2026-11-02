# M5b.2 reusable coordinator/control validation

Task2 implemented within this support-layer scope; public audio elements are next.
`framelink_gst_session` builds independently of sample application internals.
`runtime/` owns common authentication/discovery; sample compatibility includes
preserve existing callers. Existing Android application CMake sources updated.

Public headers: gst/framelink_session_context.h, gst/framelink_session_transport.h.
Applications/providers supply authorized connected STREAM sockets and protected
32-byte credentials to bounded synchronous worker APIs. A new explicit v2
negotiation binds both offers, selection, fresh nonces and direction through HMAC.
Audio uses a separate connection with a credential derived from full config;
stale generations fail. Failure closes the connection and cancels the session.
No PCM is transported by these control functions and no secret is logged.

The typed owning GstContext propagates through ordinary GstBins; one coordinator
holds required-track readiness, one immutable generation clock mapping, and finite
cancellable waits. A new generation resets readiness/mapping. Application-supplied
clock mapping is validated, not measured or inferred. Calling configure directly
validates policy but is not authentication; network callers use negotiate_stream.
Wait callers retain their session reference and do not wait in synchronous
set_state callbacks. Standard elements still require integration in later tasks.

Validation:
- Linux9/9: core v1/v2/C11, context, transport, rendezvous/auth/connected-FD,
  existing M3 producer/consumer application scenarios (28.91s total).
- Windows8/8: corresponding suite without POSIX connected-FD (30.71s).
- Android tablet5/5: v1/v2/C11/context/transport executables. arm64/x86_64
  support builds and signed-app JNI targets build. These loopback control tests
  do NOT qualify new public cross-UID media elements.
- Negatives include wrong credential, missing/incompatible tracks, invalid
  selection, reflected producer proof, unauthenticated selection, stale audio
  generation, silent peer deadline, caller cancellation and context type mismatch.
- Exact37ms shared mapping preserved; conflicting per-generation mapping rejected.

Initial Android command requested desktop-only fl_av_auth target after building
both new tests successfully; unavailable target was removed from the command.
This was a build invocation correction, not a skipped claimed device test.
No new public audio factory, media playback, physical timing or zero-copy PCM
claim is made here. Tasks3–13 remain.
