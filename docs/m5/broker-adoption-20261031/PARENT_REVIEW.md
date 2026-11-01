# Parent review — 2026-11-01

Reviewed source `6b37195` and evidence `5b40819` directly. Accepted as the
bounded broker/control/audio adapter increment, not Android cross-UID A/V.

## Review

- Producer bounds, mode-3 exact media budget and startup/idle deadlines remain
  consistent with the current desktop API. Modes 0–3 and PRIMED/GO are retained.
- GCancellable and retained appsrc references wake owning-thread I/O/unwinding;
  the watchdog is joined before resources are destroyed. The embedded entry
  point does not terminate the host process. Provider callbacks still own their
  finite deadline; cancellation is not a promise to interrupt arbitrary code.
- STREAM grants are signature-protected, use separate C/A connections, and
  shutdown retained descriptors on retirement. Workers alone close their FDs.
  Replacements and stale revoke are scoped to the active owner token.
- Reviewed the preserved first device failure and corrected oracle: client death
  may produce socket EOF before Binder death. Explicit peer EOF plus retirement
  is valid there; the separate remote-owner test still requires owner-death.
- Existing Android build and 10/10 device logs were inspected, not rerun here.
  They establish the lease/security seam, not production media or physical sync.

## Fresh validation

Matching Linux Release build succeeded without source changes. Eight affected
CTest cases passed, 102.12 seconds total:
`av_connected_fd`, `av_auth`, `m3_av_apps`, `av_offer_bounds`, `m5_finite_budget`,
`m5_startup`, `m5_preconnect`, `broker_producer_stream`.
See `parent-linux-tests-20261101.log`. `git diff --check` passed.

## Next dependency

The native video backend still resolves a private pathname. Next implement an
owning, validated connected-SEQPACKET descriptor entry point with bounded
handshake/revocation and held-buffer lifetime tests. Bind that into signed APK
SessionProviders before claiming actual cross-UID video/GDP audio parity.
M5 remains open; no Windows performance or physical-output claim is added.
