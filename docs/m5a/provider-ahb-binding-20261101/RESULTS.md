# Task 7 partial: actual Android RGB AHB pool binding

Exact API31+ AHB allocation IDs, ordered native RGB descriptors and full session
form a canonical binding. Owner retains immutable pool through session lifetime.
Producer temporarily reserves every slot, inspects imported AHB IDs and releases
reservations on all paths. No payload map/copy or GPU wait.

TCL API35 device RGBA/RGB10 cases passed: owner/imported digests match after native
SEQPACKET handle transport; substituted allocation differs; aliases and opaque
YUV-as-NV12 reject; partial busy-pool inspection cleans reservations; failed output
unchanged. arm64/x86_64 API29 builds passed (runtime ID requirement remains API31+).

This is native resource identity evidence, not a signed cross-UID SessionProvider
A/V run, GPU import/completion capability, or throughput qualification. AHB exposes
no portable GPU UUID/opaque plane layout; this helper does not manufacture either.
Endpoint GPU capability requires independent actual import/completion evidence.
