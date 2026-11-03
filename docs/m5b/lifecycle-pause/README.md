# Task6 partial: pause interruption and context rejection

Before-fix test reproduced a coordinated between-frame pause cancelling the shared receiver session. Source unlock now interrupts local reads rather than revoking the session; unlock-stop resets the local cancellation token. Stop/flush still cancels the session. Readiness waits check local interruption in at most 50ms slices without changing the whole startup deadline.

The test pauses both producer and consumer after delivery, resumes the same session, and verifies exact subsequent pixels/PCM/timestamps and EOS. All four public elements reject a malformed typed session context before falling back to an uncoordinated/standalone endpoint.

Linux affected5/5 plus extended context test; Windows5/5; Android3/3 passed, both ABI/JNI builds passed. These checks do NOT qualify arbitrary in-flight pause, coordinated remote pause signaling, partial-record cancellation, reconnect, or complete task6. Those remain next work; milestone unmerged.
