# M5a.7a Linux native source — partial integration

- Linux affected regressions 7/7 (native source plus prior timing/pool/stats).
- Final expanded native-source test passes RGBA and NV12: same DMA-BUF inode,
  exact pixels/PTS, two-slot bounded ownership and reuse after all references
  release, shared-memory-subview survival after source shutdown.
- Sysmem-only downstream rejects native caps; no silent source copy fallback.
- Windows affected legacy regressions 7/7; no Windows native-source claim.
- Android existing policy suite passed; arm64/x86_64 static plugin builds passed.

The strengthened reuse test initially timed out because appsink retains its
current sample until the next pull. Publishing/pulling the second reserved slot
releases that reference, after which the first slot becomes reusable. This is
expected GstMemory ownership, not permission to ignore downstream references.
Source-side CPU readiness waits remain. Tests map pixels solely for verification.
Remote session agreement, native sink/decoder, GPU consumer integration and
other-platform native sources are still pending. See GST_NATIVE_MEMORY.md.
