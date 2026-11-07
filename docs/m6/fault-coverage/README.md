# M6.4 existing-policy fault coverage

Eight selected regressions passed on each of Linux, Windows and Android. No public overload default changed; D2 remains unresolved.

- gst_pool_policy: pool2/3, all slots held, explicit zero-wait drop versus bounded error versus release-before-deadline recovery. Exactreceived/submitted/drop/ring-full counters; boundedcompletion. Includes no-fill, producer-gap versus sequence-gap and clock scheduling controls.
- receive_queue: bounded unread replacement and identities; no permission to overwrite leased storage.
- frame_metadata/contract: validation, sequence/timestamp/metadata identities and malformed/stale rejection.
- av_memory_adversarial: malformed authenticated negotiation, identity/generation/proof/usage rejection with unchangedoutput and boundedtime. Not a runtimeGPU timing test.
- gst_audio_sink/source/local_audio: exactPCM/PTS/duration, explicitGAP/DISCONT preservation, invalidsegments/caps/framing, partialpacket/pause, authenticatedidle failure, endpointclosure, boundedbackpressure/teardown. Idle/no-samplesfailure is transport underrun behavior, not physicalspeaker underrun measurement.

Real EGL/D3D11 importedallocation completion/readback and producerdeath checks are in lifecycle/ownership; EOS/pause/consumerdeath cancellation in lifecycle/eos-fix. A new Windows queue-side GPUwait negative is pending: holdactualCopyResource/completion behind a sharedfence, provelease remainsbusy andproducerblocked, signalfromindependentdevice, then verifycompletepixelreadback. Softwareheld-reader tests alone do not prove delayedGPUqueue behavior. Defaultpolicydecision, platformfaultcoverage gaps andwholemilestone acceptance are notclosed here.
