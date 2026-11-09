# Checked coordinated native video relay

**Windows focused build/diagnostic and 6/6 regressions passed (23.99s).**

Base `3bb31b489a8516808091eb9a2346d24b3c9928c6`; exact changed-source hashes and Windows commands are recorded in `status.json` / `run.py`.

`SessionNativeRelay` composes the session contract with the existing same-allocation native relay. It validates configured video geometry/format before reserving the downstream pool, checks both immutable session snapshots before every publication, and rejects missing/mismatched media generation or untimestamped frames. Preflight/metadata rejection leaves the frame caller-owned; native submission failure can already hold a lease and must follow NativeRelay ownership rules. Every error permanently stops further publication. Sessions/channels are borrowed and require a single serialized owner; this is not concurrent-reconfiguration atomicity.

Drain deliberately remains possible after session cancellation: `finish()` stops publication but returns BUSY while a downstream reader still owns a texture. `collect()` returns upstream ownership only after the existing native ring proves actual downstream completion. Ring-generation changes and native errors still fail closed. No forced lease completion, hidden copies, public DLL symbols, wire formats or defaults changed.

Focused negatives: stale media generation; upstream cancellation before publication; downstream cancellation before publication; downstream session replacement; upstream cancellation with an outstanding downstream read lease. The first four require zero downstream frames, no transferred upstream lease, retained caller-owned input and rejection of a second publish attempt. The fifth requires BUSY drain and producer backpressure until downstream completion, then successful safe drain. Existing nonempty/empty two-session A/V checks retain exact pixels, metadata, PCM, 37ms offset, barriers and terminal accounting.

Scope: this is the checked **video leg**, not a complete relay adapter. PCM forwarding, authenticated GAP/FLUSH/END ordering, and terminal joining still belong to the fixture/controller and must be composed next. Independent-process A/V, real-decoder multi-hop, Windows timing and lifetime acceptance remain open. Linux/Android campaigns are deferred; only Windows uses the new sample. D2/D3 unresolved; API not frozen; M6 open; M7 hold. Loom pre-edit context unavailable because chromadb is missing.

A comment-only ownership clarification followed the focused run; executable source unchanged. Final source hash is in `final-source.json`.
