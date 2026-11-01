# Authenticated GStreamer allocation policy

Task M5a.7 final integration slice, 2026-11-01.

The SessionProvider applies both authenticated endpoint offers to adopted source
and sink elements before starting media. The public `GstFrameLinkMemoryPolicy`
bundle is an in-process trusted-control API, not an authentication substitute.
Platform binding adapters independently verify the immutable resource pool.

Native ALLOCATION requests must carry encoded memory requirements; these are
checked against both authenticated endpoint policies and the selected paths.
Missing requirements, unsupported hard usage and disallowed copies reject.
Pool responses expose the selected resource, operation mask and session generation.
Native readers must explicitly promise to retain memory through GPU completion.
A stock consumer without that adapter contract is rejected, not assumed safe.

Policy application freezes element properties and the actual pool generation.
Changes before startup, during source delivery or before sink submission fail
closed and require a new authenticated session/element. Legacy source geometry
renegotiation remains unchanged. These checks do not add a frame queue.

## Validation

- Linux integrated affected tests: 7/7 passed before final generation hardening;
  final source/sink startup and in-stream generation regression passed.
- Windows final affected suite: 7/7 passed, including both source and sink
  generation negatives; results in accompanying log.
- Android initial five device executables and both ABI builds passed; final
  generation/native-producer device recheck passed; final arm64/x86_64 builds passed.
- Cross-app native libraries compile for arm64 and x86_64. This is not a new
  signed cross-UID GPU-native runtime qualification.

Generation regression covers legacy resize, authenticated source resize during
playback and before startup, sink resize before startup and before submission,
and explicit native-reader completion/usage agreement versus missing agreement.
Native producer fixtures cover all-platform policy application, frozen properties,
ALLOCATION usage negatives, original resources, metadata and reference lifetime.
Linux broker fixtures exercise authenticated policy before A/V media delivery.

## Qualification limits

Supported explicit adapters are locally validated, not a generic decoder claim.
Linux supports qualified LINEAR DMA-BUF layouts; Android native RGB uses exact
AHB allocations and rejects opaque YUV-as-NV12; Windows requires canonical NT
exports and actual matching device/import checks. Caller-advertised usages are
not driver probes. Actual GPU completion remains the adapter's responsibility.

Windows GPU decoder example is M5a.8. Cross-process relay, performance/copy/latency
qualification, signed native Android full-pipeline qualification and final review/
CI/merge remain later gates. No physical output or throughput claim is made here.
