# Task 5 timing acceptance

Common running-time mapping, distinct input TIME segments, exact intentional +/-37 ms A/V offsets, PTS/DTS/duration and content are covered by common-timeline and video-events evidence. Audio GAP, video GAP, DISCONT and coordinated EOS preserve that timeline; empty-eos covers zero-media terminals without fabricated samples.

Final controls: rate2 input rejected on both audio and video; midstream audio segment replacement rejected; fresh coordinator generation clears old mapping, rejects stale mapping updates, and accepts a new common mapping preserving 37 ms offset. Invalid/missing receiver mapping and mapped video interval overflow reject. Authenticated remote flush cancels the current generation; it never silently rebases either track.

Linux selected 4/4, Windows 4/4, Android updated tests 3/3 passed; Android event-channel test was already passed in video-events. Both Android ABI builds passed. Production code is unchanged from the empty-eos increment. A deliberately invalid segment followed by more media caused a GStreamer ordering warning during test development; the final negative control observes the rejection before sending further media.

Task5 is locally validated, not merged. Actual element reconnection/pause-resume, cancellation across all waits and transport security/lifetime qualification remain task6. New epochs require fresh generation/authentication, not a live seek. Elements are forward-rate1/nonseekable; no trick playback or physical synchronization claim.
