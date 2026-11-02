# Task6 partial: rapid audio pause/resume

A deterministic partial-header fixture sends seven GDP bytes, observes them at the parser input, pauses/resumes, then sends the remainder. Before repair the reader sometimes remained stopped (7 observed versus999 expected). Interrupted reads now wait through GstBaseSrc's playing gate and retry when resumed; actual stop returns flushing. The loop preserves bounded parser state and avoids recursive retry.

Windows repeated full A/V testing then exposed already-read byte loss before source-pad delivery (video arrived, audio timed out). The byte reader now retains only its current <=4096-byte GstBuffer until source-pad push, reusing it after pause instead of reading ahead. Stop/finalize releases it. This adds a reference to existing bytes, not an unbounded media queue.

Final validation: Linux20 audio +20 A/V, Windows20 audio +20 A/V, Android20 audio +20 A/V repeat invocations passed with fatal warnings. Both Android ABI/JNI builds passed. Header fixture checks exact PCM/PTS/duration; A/V checks add zero-delay consumer pause to prior between-frame/in-flight cases. Earlier failing header and Windows results retained.

Task6 remains open for coordinated fatal-error propagation, recovery/reconnect and broader security/lifetime gates. No physical output or arbitrary upstream losslessness claim.
