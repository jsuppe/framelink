# BGRA CPU 4K30 bounded-output-cache sustained validation

Runtime29bbddb CPU output cache; exact component revision and binary hashes in status.json/run.log. Full Windows112/112 passed. Untraced1800s, unchanged pool2/unread1/output1,latency0,transport-only,no-output-preroll,34ms diagnostic and64KB/min resource limit.

54000video/86400000PCM,180000contiguousaudio, complete queue accounting, zero video/audio misses, inferred A/V mapping delta0ns. Video arrival p9515.757ms/max19.367ms. Consumer privatecommit7.14KB/min versus previous98.11; producer7.18KB/min. Both pass64KB/min. CPUconsumer23.59%,producer13.74%; handles/threads effectively flat.

This repaired revision passes the previously failing BGRA CPU resource cell. Isolated A/B established reduction in allocation/pagefault churn but did not reproduce retained growth, so we do not claim a proven leak or precise retained-growth cause. Historical failure remains recorded. Other CPU formats/rates require sustained regression at29bbddb. Original native159ms intermittent stall remains unresolved; no physical playback or end-to-end zero-copy claim.
