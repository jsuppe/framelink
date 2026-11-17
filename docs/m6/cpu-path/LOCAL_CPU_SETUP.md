# Authenticated local CPU setup

Linux/Android explicit fl_gst_session_open_local_cpu composes protected FAR2
Unix discovery, exact authenticated session negotiation, clock mapping, PCM and
video event stream authentication, authenticated SEQPACKET transfer and CPU
pool binding. Both callers explicitly authorize shared-CPU COPY. Native/TCP
setup remains separate, with no fallback. Video is required; pool/queue limits
match existing setup, contiguous packed/NV12 planes are validated by CPU pool
setup. Whole setup cancellation also interrupts pool handshake; media retains
the separate idle deadline. Existing local ready/drain handshakes apply.

Four added full A/V cases (BGRA/NV12, payload and empty) begin with the consumer
before producer and use independently discovered/authenticated endpoints. Exact
pixels/PCM,37ms relative A/V timestamp offset, ordered EOS and ready/drain pass.
Absolute receive PTS is checked against the installed session mapping; this is
not a physical-output timing measurement. Four absent-peer/cancel negatives
and incompatible-format negotiation reject without a claimable CPU channel.
Existing twelve broker-bound thin/bin cases remain passing.

Linux5/5, Android4 runtime binaries and both ABI builds passed; Windows3/3
transport/auth/discovery compatibility checks passed. Android rendezvous still
reports the two documented filesystem fixture capability exclusions. No new
failures in this step. Evidence and source hashes: local-cpu-setup/.

This helper can provision contexts for bins but shared-cpu plus session= is
still rejected by bins. Next is direct bin worker integration, not rate or
endurance acceptance. M6 remains open.
