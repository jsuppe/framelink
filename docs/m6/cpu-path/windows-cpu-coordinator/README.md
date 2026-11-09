# Windows shared-CPU coordinator broker seam

Windows now exposes additive `fl_gst_session_bind_shared_cpu_pipe` and the existing
portable single-claim API. The caller must authenticate/delegate the exact local
OVERLAPPED message pipe and pinned peer process. This is **not authentication or
local discovery**, and does not enable GStreamer CPU elements/bins on Windows yet.

The coordinator consumes the pipe on every path, enforces exact configured
video generation and role, and requires metadata + ordered EOS. A manual-reset
cancellation event is duplicated into the factory/channel. A local duplicate
prevents concurrent replacement from closing a handle before factory adoption.
Cancel/reconfigure/finalize signal cancellation; claimed channels and held mapping
leases remain caller-owned. A stale completed factory cannot publish into a newer
generation. Binding does not mark a track ready. POSIX FD behavior is unchanged.

## Validation

Final Windows **6/6** pass (6.93s); Linux **4/4** and Android **4/4** affected
shared-code regressions pass, including A/V, with both Android ABI builds.
`final-provenance.json` pins all changed Windows source files. The staged Windows
tree started at d8f97c2 and was then modified: do not confuse it with the archived
exact-d8f97c2 candidate artifacts. A fresh exact-source integrated gate follows.

The new coordinator test runs one warm session plus **100 measured sessions**
with exact process-handle equality after every cycle. It checks role/generation,
double-bind/claim rejection and consumed invalid inputs, mapped pixels after
cancel/replacement, metadata, EOS vs peer death, and cancellation of 60s setup
under1s (both cancel and replacement). These in-process paired endpoints do not
replace real multi-process/performance/driver-lifetime qualification.

## Preserved initial negatives and causal fixture correction

Early resource assertions failed. Plugin-path isolation and removing gst_init
alone did not solve it. A no-work control showed314→2468 handles in about1s before
any coordinator work. Kernel handle snapshots identified newly opened **Key**
objects under Windows application-registration Classes entries, not CPU pipes or
sections. GIO initializes its Windows app-info registry cache asynchronously.
Calling `g_app_info_get_all` and freeing the returned list joins that initialization
before the test's warm baseline; all100 later cycles then have exact handle
cleanup. No production resource limit was relaxed. Diagnostic logs/source and
initial failures are retained. This finding is **not** proof that earlier native
D3D/device reconnect growth is the same mechanism; that acceptance gate remains.

Raw builds/preflight: `<linux-home>/dev/evidence/m6-cpu-path/windows-cpu-coordinator`.
Loom CLI was unavailable on PATH; manual ledger/source review used. No subagents,
no deferred Linux/Android campaigns, no public overload/fallback default changes.
Next: Windows GStreamer CPU integration and authenticated discovery/delegation,
then coordinated D3D11 native producers and outstanding timing/lifetime/fault gates.
M6 open; hold before M7.
