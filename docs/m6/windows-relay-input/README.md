# Explicit source-time relay input and separate-process integration

Prepared against backend `d4c32cc` snapshot/bounded-open contract. New intermediate receiver API installs a source-relative forwarding mapping during authenticated FAR4 setup; final receiver mapping/defaults unchanged. Shared session code compiled locally; focused Linux session transport/context 2/2 passed in 1.13s (raw LastTest.log retained). This is not validation of Windows-only branches.

Windows-only work pending compile/runtime: additive relay-input C11 symbol probe; independent FAR4 source child; FAR5 serialized event control; three-process raw native relay using graph-lifetime consumer snapshot and real read leases; bounded producer attach in FAR5. No inherited texture handles. All source pixels/PTS/DTS/duration/generation checked at final reader. The raw three-process fixture negotiates A/V capabilities but does not send PCM: do not claim three-process A/V qualification. Separate live A/V/event fixture remains its own focused oracle.

Parent backend worker owns the Windows execution lane. No Windows build/test was launched by primary in this phase. Prepared tools/validate_local_native_relay.ps1 requires explicit lane handoff and exact clean revision, selects six named checks, and preserves failure evidence. Its Windows parse/execution is also pending. No full suite, ABI freeze, throughput or lifecycle acceptance claim.

## Collected Windows focused validation

Exact clean `486bf04510730a11688f5b1a25a5294767fdfd4e` configured and built successfully; all six named checks passed in 4.10s. See status.json, targeted.log and LastTest.log. Includes actual separate-process video/snapshot/lease check and separate local A/V/event check, not three-process PCM. Prepared PowerShell runner executed successfully; source SHA256 manifest retained in external evidence `windows-relay-focused-486bf04/relay-evidence-486bf04`. Backend d4c32cc results collected separately without repeat. Actual three-process PCM integration is now in progress.

Rebuilt DLL export-name comparison retains all119 baseline names and adds9 (128 total), including explicit relay-input and the5 backend snapshot/bounded-open symbols. See exports.json. Name retention is not ABI/layout/old-client qualification.
