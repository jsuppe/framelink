# M5b.2 coordinator foundation (partial)

Optional-track v2 config preserves v1 ABI and wire behavior. Typed GObject
GstContext owns the coordinator; local required-track readiness, stale generation
rejection and finite cancellable waiting are implemented. No media I/O threads.

Linux4/4 and Windows4/4: session_contract, session_contract_c_api,
session_optional_tracks, gst_session_context. Android tablet same4executables
passed; arm64/x86_64 builds passed. Includes malformed/absent fields, wire
magic/version downgrade, replacement track-set changes, context reference lifetime,
missing-track timeout and cross-thread cancellation. First Linux build command
regenerated CMake targets mid-invocation; new target was initially not built.
Explicit configure/build repaired test invocation; no source failure masked.

M5b.2 is NOT complete: authenticated negotiation/provider extraction and pipeline
context propagation still pending. Configuration API validates offers, but is not
itself authentication. No public audio factories, network/session clocks or
physical-output qualification established by these tests.
