# Explicit automatic FAR5 receiver bin

`framelinkavsrc::set-local-native-pool-candidate` selects FAR5 receiver setup on the existing asynchronous worker. Existing `set-local-native-candidate` remains FAR4. NULL-only copied/frozen producer-ordered candidate, explicit A/V/pool size, no implicit fallback or profile rebinding. No public C layout/wire/default change.

Final Windows positive/negative/raw A/V batch3/3 passed3.41s. Eight affected native relay/bin/event/cancellation/replacement checks passed14.72s. Shared Linux plugin built and source/sink A/V bin checks2/2 passed39.16s. Independent review found no blocking issue.

The new three-process control uses the actual automatic avsrc receiver, no external context/adopt call. Independent checks cover8 native pixels/metadata,384 PCM frames/1536 exact bytes and37ms mapping. Final GstMemory is retained across actual EOS and receiver bin NULL, re-mapped for pixel checks, then really released before the relay drains. Producer/reader process lifetimes are held through real collection.

`SessionPcmRelay::endPublication` is explicitly distinct from strict `finish`: it validates real upstream PCM EOS, forwarded authenticated video END/count/PTS and pending events, but permits native reads to remain pending. It never completes a native lease. The caller must keep the graph and perform native collect/finish after actual final-reader release. Strict finish retains its previous requirement.

Failures are retained: raw per-frame retirement deadlocked against appsink current-sample retention; first bounded in-flight adaptation checked pending count before collecting previous release; control drain attempted after bin NULL had closed the control session. Corrected fixture permits one in-flight sample, collects before checking final pending count, accepts genuine control drain before teardown, and never treats either EOS or control drain as native completion.

Negative controls reject missing/invalid candidates, video-only/unspecified/invalid pools, CPU/native mismatch, mixed profiles, repeated binding/adoption and FAR4 peer mismatch. A60s absent-peer setup cancels to NULL within1s. No broad suite, end-to-end timing, resource or API freeze claim. The intermediate source-relative/snapshot/same-allocation composition remains explicit.

Final explicit strict-finish assertion (TIMEOUT with pending native read, then endPublication success) rebuilt and passed1/1 in1.20s; exact final source hashes retained in strict-finish/.
