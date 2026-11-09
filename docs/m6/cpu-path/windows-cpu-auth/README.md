# Authenticated Windows CPU pipe binding

Additive `fl_gst_session_bind_authenticated_cpu_pipe` authenticates on the exact
connected local OVERLAPPED message pipe before pool setup. It borrows a pinned
peer process and a protected nonzero 32-byte root credential, consuming the pipe
on every outcome. Caller still performs connection/discovery and credential
provisioning. Unrelated STREAM authentication is not accepted as pipe authority.

A distinct tagged handshake carries fresh 32-byte nonces and directional HMAC
proofs binding the full configured session, both nonces, producer/consumer roles,
and kernel-verified process IDs. The peer process remains pinned across auth and
pool handle import. Authentication has one finite deadline; subsequent pool I/O
uses the existing per-operation bound. Session cancel/reconfigure interrupts
both stages via the existing generation-owned cancellation event. Failed auth
publishes no CPU channel; there is no downgrade to legacy/broker-only negotiation.
Legacy public APIs, CPU layouts, native defaults, and POSIX behavior are unchanged.

## Validation

- Windows focused suite **6/6** passed. Final producer-drain fixture refinement:
  **2/2** affected tests passed, with full output in drain-verbose.log.
- Three independent child processes exchange 24 exact frames, metadata and ordered
  EOS after authentication with distinct kernel PIDs. Producer waits for consumer
  closure after EOS, retaining process lifetime through terminal drain.
- Five lifecycle modes warmed, then **100 cycles** require exact process-handle
  return after every cycle: held mappings, cancel/reconfigure, single claim,
  metadata/EOS and peer death. Blocked 60s authentication cancels/reconfigures in
  under1s. Eight wrong-key/config/role/absent-key/downgrade negatives and a silent
  peer reject with no published channel. No replay-injection test is claimed.
- All **12 Windows A/V cases** now authenticate the real CPU pipe, alongside their
  existing separate PCM/event authentication. BGRA/NV12, thin elements/bins,
  common A/V mapping and EOS behavior pass. No target-rate qualification inferred.
- Linux **3/3**, Android **3/3** focused shared-code regressions pass; both Android
  ABI builds pass. These are shared-change checks, not resumed platform campaigns.
- Session DLL preserves **all31 exports**, adding exactly one new API. See
  api-compatibility.json and raw old/new dumpbin logs. No API freeze claimed.

## Preserved resource negative

The initial one-clean-cycle warmup failed on the first cancellation lifecycle:
1472→1473 handles. Kernel snapshot identifies the extra object as a Windows
`WaitCompletionPacket`, not a pipe/section. Idle/HMAC/RNG controls on main and
worker threads did not reproduce it: its owning component remains unresolved.
All five lifecycle paths are now warmed before the measured100 cycles. The final
cross-process fixture also exercises these runtime paths before measurement;
its five warm counts remain1475 and every measured cycle returns exactly1475.
This establishes bounded *warm* lifecycle behavior only. It does not waive the
cold one-handle observation or any native D3D reconnect/memory acceptance gate.
Initial failure, diagnostic source and all control results are retained here.

## Scope / next task

This is authenticated binding, **not automatic endpoint creation/discovery**.
Next wire up Windows protected local endpoint discovery/connection to this exact
pipe proof and the existing coordinated CPU setup. Then continue Windows D3D11
producer/session integration and remaining timing/lifetime/fault acceptance.
M6 remains open, D2/D3 product decisions unresolved; hold before M7.

Raw evidence: <linux-home>/dev/evidence/m6-cpu-path/windows-cpu-auth.
Source hashes: final-provenance.json. Loom command was unavailable on PATH;
manual authoritative-ledger/source review used, no subagents.
