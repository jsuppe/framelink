# Windows named CPU endpoint prerequisite

Internal local-only OVERLAPPED message-pipe rendezvous: current-user SID-only
protected DACL, first-instance owner exclusion, no remote names, no inheritable
handles, client-first start, finite connect/accept cancellation and pinned
kernel peer-process handles. The name, SID and PID are NOT authentication:
the existing exact-pipe challenge must succeed before any pool exchange.

Windows build and five focused regressions pass. Three independent child-process
sessions provision endpoints using this helper then authenticate and validate
eight complete frames/metadata/EOS each. Tests also cover owner collision,
remote-name and byte-mode rejection, absent-peer timeout, accept timeout,
60s waits cancelled in under1s, actual kernel DACL/one current-user ACE,
noninheritability, ten client-first/already-connected cycles with exact handle
cleanup, and existing 100 authenticated session/lease cycles and hostile auth.

The first expanded ACL check used GetSecurityInfo and failed exact handle count:
1478 baseline,1480 before ACL,1488 after ACL,1486 after endpoint cleanup.
Preserved diagnostic shows the growth occurs in the ACL inspection interval.
Replacing only this test-side query with GetKernelObjectSecurity removes the
growth while checking the same actual DACL. No production threshold or ownership
change; original failures and diagnostic source retained.

This is a tested internal primitive, not yet wired to public local session
discovery. No public API/default, Linux/Android code or deferred campaign changes.
Next compose it with configured session authentication/cancellation. API not
frozen; M6 open; M7 hold. Loom unavailable (chromadb missing).
