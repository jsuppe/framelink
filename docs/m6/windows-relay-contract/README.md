# Windows native receiver-to-producer relay contract prerequisite

Base `8025ddd1fe29ac4d03bb95f94f0950349094d438`; exact two test overlays
and hashes in status.json. Build, explicit diagnostic and 5/5 Windows focused
regressions passed. No production code/API/default changes.

Two automatic authenticated native A/V sessions (both EOS submission orders)
delivered 16 first-hop D3D11 frames with exact pixels, PCM bytes, metadata and
37ms offset. Submitting each imported receiver buffer to an independent second
producer returns FL_INVALID, publishes no frame, and leaves both second-hop
reservations available. First-hop pixels, timing and retained-memory teardown
checks still pass after rejection. This is a negative interoperability boundary,
**not successful two-hop delivery or M6.9 acceptance**.

The implementation gap is concrete: FrameLinkD3DProducer::submit requires its
own unpublished reservation; native receiver memory is a reader lease, not that
reservation. Automatic local sessions allocate independent rings. Do not remove
this guard or add a hidden GPU copy to make a relay pass. Existing NativeRelay
supports explicit common external D3D11 allocations with a canonical NT-handle
identity matcher and propagates downstream completion to upstream leases. Next
work is integrating that explicit same-allocation path with two coordinated
sessions, authenticated PCM/events and common-timeline mapping. Evelyn owns
this ready implementation; no external approval is needed for the existing
strict profile. D2/D3 default decisions remain separate and unresolved.

Loom pre-edit context was attempted; unavailable because chromadb is missing.
No deferred Linux/Android campaigns run for these Windows-only test changes.
