# Explicit Windows native A/V bin sessions

Implementation is the commit introducing this document, on `c8ff226`.

`set-local-native-candidate(pointer)` on both A/V bins copies a provisional,
producer-ordered policy template while NULL. Configure all properties first:
nonempty same `session=`, video enabled, producer `native-memory=true`, receiver
`native-memory=false`, neither `shared-cpu`. Both peers explicitly authorize
D3D11 GPU_SAMPLE producer operations=0 and CPU_READ receiver COPY|READBACK.
The action freezes configuration but does not claim authentication. Worker setup
uses FAR4 discovery and authenticates the exact candidate before native allocation.
The bins adopt channels and apply the negotiated generation's policy before child
startup; the receiver swaps endpoint requirements AND paths. The producer's
pipeline base time is the authenticated session epoch. Media remains behind the
existing consumer-ready gate. No fallback or public overload default changed.

Each candidate-bound bin permits one setup attempt. Reconnect, including after
cancellation or failure, requires new bins; bound child policies are never silently
replaced. Existing unbound/CPU/legacy reconnect behavior is retained. Mixing broker
adoption/policy with a local candidate rejects. Other platforms return false for
this new action and keep existing modes.

## Verification

- Windows **6/6** focused tests passed, **17.30 s**, exact four executable-source
  overlays on the previously clean c8ff226 tree. Source hashes/commands retained.
- Three new automatic bin cases: both EOS orders and empty streams. Real D3D11
  pool allocation through the producer bin, original texture identity, exact
  pixels/PCM, DTS/duration, exact 37 ms A/V mapping and producer pipeline epoch.
  Caller candidate storage is overwritten after acceptance to check ownership.
  Bounded teardown and same-bin restart rejection verified.
- Twenty configuration controls cover malformed/unauthorized templates, missing
  discovery path, wrong track/memory selections, null candidates, repeated
  binding, broker-action mixing and immutable properties. Both producer and
  receiver cancellation interrupt absent-peer 60-second setup deadlines within
  one second. Existing local helper mismatch/legacy-isolation negatives retained.
- Affected shared-code regressions: Linux **4/4** (42.83 s), Android **3/3** runtime
  and both arm64/x86_64 builds passed. These are bin/CPU regressions, not resumed
  deferred rate campaigns. Initial Windows pass also retained; no failed run.
- Review preserved legacy sink mutation behavior after nonnative channel adoption;
  separate adoption bookkeeping only prevents mixing the new candidate action.
- Clean exact integrated Windows build/API/full suite is the next owned check.
  Loom briefing unavailable because chromadb is absent; no delegation used.

This is synthetic native producer integration with EXPLICIT receiver readback,
not end-to-end zero-copy, arbitrary decoder qualification or a rate/resource pass.
D2/D3, original uninjected 1529 ms startup failure, lifetime and acceptance gates
remain open. API not frozen; M6 open; maintenance hold before M7.
