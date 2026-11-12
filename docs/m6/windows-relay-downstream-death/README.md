# Actual downstream-process death: fail closed without fabricated drain

Base `9e81eb26044c9cb1a6b1d88e56156cc4258f8aec` (production code c5bea11), with the three test/build-file hashes pinned in status.json. Windows-only test addition; no production code, public API, wire format or default changes.

The supervisor owns three independent source/relay/final-reader processes and original D3D11 allocations. The final reader verifies the original allocation identity, exact first frame pixels and metadata, 48 stereo PCM frames (192 bytes), and a common 37ms A/V offset. It keeps its read lease live and signals readiness. The supervisor forcibly terminates that reader (confirmed exit73), rather than invoking session cancellation or normal teardown.

Acceptance was fixed before execution: event failure within1.5s, exactly one forwarded frame/PCM block, publication rejected, no synthetic video END, and no fabricated downstream consumption. Native finish must return DISCONNECTED with its upstream lease still pending; later collect returns INVALID, not success. The remaining source is explicitly stopped (exit74) before the relay closes the old graph (exit0). This does **not** prove successful native drain, automatic upstream cancellation, safe reuse of dead-generation pools, GPU-work completion, reconnect, or lifetime endurance. Those gates remain separate. Discarding the graph is not a product overload/default decision.

- Initial focused test **1/1 passed**, 2.02s. Event failure raw **34.188ms**, GstMemory **21.704ms**.
- Affected producer/relay suite **8/8 passed**, 33.13s, including existing empty/nonempty process relay, reservation, decoder, producer-death and explicit-cancel controls. New case raw **23.692ms**, GstMemory **22.227ms**.
- Full exact-revision Windows integration is the next owned validation; these are not its results. Portable runtime files are unchanged, so no unrelated Linux/Android campaigns were rerun.

Raw evidence: `<linux-home>/dev/evidence/m6-cpu-path/windows-relay-downstream-death-20261112`; source overlay retained there. The existing c5bea11 Windows checkout was overlaid for focused tests; its earlier clean integrated results are separately archived and not relabeled. No failed assertion in these runs. Loom pre-edit briefing was attempted (CLI does not accept --json; normal context then failed for missing chromadb).

Next: exact integrated Windows build/API/full141/diagnostics, then remaining automatic-bin/producer integration. D2/D3 unresolved; API not frozen; M6 open; M7 hold.
