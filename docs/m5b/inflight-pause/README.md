# Task6 partial: in-flight consumer pause

Causal evidence: pausing the consumer after enqueue can make GstBaseSrc discard a buffer returned by create(). Producer stats showed received5/submitted5/dropped0; consumer missed video while audio arrived. Keeping one reference to the already-acquired buffer until its source-pad push permits retry after pause. This is not an added target queue or frame copy. Stop/finalize releases the reference and native ownership normally.

The corrected boundary test pauses only the consumer while the producer submits normally. A separate between-frame case pauses both pipelines. Upstream appsrc can discard its own popped buffer when paused before FrameLink; that is outside transport acceptance. Appsrc can also transiently return FLUSHING during resume: the feeder retries only rejected pushes, for at most100ms. The test does not pretend that a push is transport acceptance.

Linux20/20, Windows20/20 and Android20/20 repeat invocations passed with exact subsequent pixels, PCM, +/-37ms offsets and EOS. Both Android ABI/JNI builds passed. Disabling only the retained-buffer behavior reproduces video loss in the corrected test, with producer5/5/no drops; negative log retained. Fix restored before all platform qualification.

Task6 remains open for broader reconnect, track-loss and partial-wire/lifetime qualification. This does not claim lossless arbitrary upstream pause, physical synchronization, or remote pause signaling. Buffers accepted downstream are no longer owned by this retry mechanism.
