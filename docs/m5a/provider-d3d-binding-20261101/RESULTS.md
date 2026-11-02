# Task 7 partial: Windows canonical NT export binding

The trusted allocator issues a unique ID for each canonical shared NT export;
the broker delivers duplicates of those exact exports with session authorization.
The helper checks imported FrameLink handles against canonical references with
CompareObjectHandles, then binds ordered authority IDs, actual texture descriptors,
actual adapter LUID and full session. No pixel mapping/copy or GPU wait.

Windows/Caspar four affected suites passed. RGBA/NV12 owner/imported digests match;
fresh DuplicateHandle references also match. Substituting a different allocation
while retaining the claimed allocator ID fails before digest publication. Alias
entries reject; busy-pool inspection releases partial reservations; output stays
unchanged on failure.

This is NOT a universal GPU allocation-identity query. Independently created NT
exports of the same pixels are not interchangeable. The allocator/broker must
ensure one canonical export per allocation and uniquely assign IDs; arbitrary
caller labels are not authority. Re-export/relay outside this contract remains
unqualified. This is adapter evidence, not full cross-process broker/GStreamer
A/V qualification, decoder completion, GPU throughput or physical residency.
