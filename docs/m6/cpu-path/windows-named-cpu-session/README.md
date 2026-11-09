# Named Windows CPU session endpoint

Additive Windows API: fl_gst_session_open_named_cpu_pipe configures the existing
CPU coordinator channel from a local pipe name and protected root credential.
Consumer owns a first-instance, current-user-only message-pipe listener;
producer can start first. Kernel-derived peer process handles are pinned, and
the full-config, fresh-nonce, role/PID-bound HMAC proof runs on that exact pipe
before pool handle exchange. Neither the name nor PID authorizes the peer.

Caller supplies an already-authenticated configuration and keeps the session,
credential, descriptors and requirements alive for this worker-thread call.
Connection/authentication share one deadline (1..60000ms); later pool operations
retain the existing per-I/O timeout. This is explicitly documented, not a claim
of a whole-pool-call deadline. Session cancellation and generation replacement
interrupt connect, accept, authentication and pool setup. No channel is published
on stale/cancelled/failed setup; no fallback or default change.

Windows five focused regressions pass: three independent child sessions using
only the new public factory deliver 24 exact frames plus metadata and EOS;
four missing-peer producer/consumer cancellation/replacement cases terminate
under1s; wrong credential, mismatched configuration and missing credential
publish no channel. Existing 100-cycle held-lease/session lifecycle, authentication
negatives and endpoint/DACL/owner-exclusion checks also pass. Linux three shared
session/header/A/V regressions pass, not a deferred rate campaign.
All38 prior session DLL export names retained, one additive factory; no full ABI
freeze inferred from symbol retention. Source hashes and commands are archived.

This is named endpoint provisioning, NOT protected discovery record management,
session offer negotiation, automatic A/V bin setup or a performance qualification.
Windows local session discovery/A/V composition remains next; D3D11 native producer,
private-memory growth and timing gates remain unresolved. Clean exact-head
integration gate follows. API not frozen; M6 open; hold before M7.
