# Windows lifecycle resource acceptance — suspended, not waived

## Evidence and scope
Both100-cycle directions deliver exactcounts but fail the frozen16KB/cycle private-commit slope: persistentconsumer21.10, persistentproducer84.09. Minimal device creation/destruction reproduces31.225 without FrameLink or GStreamer wrapping; wrapped28.210; exclusive persistentdevice -0.384; DXGI Trim35.761. Actual final deviceRelease returns0 in churn probes. These controls establish a driver/device-lifecycle contribution, not complete attribution of either production slope. ReturningCOMrefs tozero is not proof of zero driver-retained allocation.

## Why no automatic cache patch
Existing API creates independently owned devices/channels, allows borrowed native device references to be retained, and supports unload after close. A process-global shared device changes queue/fence ordering across live channels. Inferring exclusive ownership from transient COMreference counts is unsafe with external references. Keeping GPU objects until DLL static destruction risks loader-lock teardown; intentionally leaking them would hide the lifecycle contract failure.

An explicit application-owned device-lifetime API could avoid device churn while preserving a defined shutdown point. It requires channel/adapter matching, removed-device refusal, external lease ownership, queue ordering and integration through the session/GStreamer APIs. An opt-in result would not qualify the unchanged default fresh-device profile. No API change or benchmark substitution has been made.

## Status and resume condition
M6.3a Windows resource acceptance remains suspended with original limits and failures retained. Evelyn owns the integration/measurement follow-up; the device/driver lifecycle behavior is an external platform dependency. Resume default-profile acceptance after a driver/backend change with matched100-cycle evidence, or an explicitly approved supported device-lifetime contract with its own ownership tests and complete measurements. Jon owns any product-contract/acceptance decision; none has been requested or assumed here. No immediate user action is needed to continue ready in-milestone work.

M6.3b general functional coverage is already recorded: native held leases, resize/reconnect and stale tokens, delayed fences/producer death, session-generation rejection, EOS/pause and bounded cancellation passed in the exact461584b integrated suite. Do not repeat those checks as missing work. The separate100-cycle held-lease/reconnect profile failed82.528KB/cycle against16KB/cycle; handles/threads were flat (see [lease-cycle evidence](lease-cycles/README.md)).

Automatic FAR5 receiver PLAYING→PAUSED→PLAYING with retained native GstMemory,
plus cancellation while paused, is now covered. The checked relay helper fix
462fa5f limits collection to missing reservations, avoiding a post-drain probe
after the real reader completed. Its [eight focused tests passed](../windows-native-pause-validation/fix-462fa5f/README.md),
including the pause case and genuine outstanding-reader death; original failure
and diagnosis remain archived. Subsequent [nine-test focused validation atb247e5d](../windows-decoder-replay-b247e5d/build-smoke/README.md)
also passed. This closes that narrow functional coverage gap, not all fault or
endurance qualification. Neither these passes nor Linux/Android3a passes close
the suspended Windows resource acceptance, the failed100-cycle profile or allM6.

## November12 deferred-reclaim attribution (not acceptance)

[Six serial diagnostic arms](device-causal-controls-20261112/README.md) separate
hardware/WARP and device-only/texture churn. Hardware empty/textures grow
63.524/61.586KiB per fast cycle; WARP0.050/0.936. Hardware memory substantially
reclaims around6s after churn stops. Three batches settle to13524/15072/14804KiB,
not monotonic retained growth. An independent live hardware anchor does not
prevent that deferred reclamation. No transport or rendering was involved;
50ms cycles/feature level11.0 differ from historical production and5s probes.
These observations identify delayed device-path retention, not exact allocation
stacks or complete cause of the failed production slopes. No sleep/cache/Trim
patch or gate change follows. Default production remains suspended, not waived.

The [explicit lifetime option](DEVICE_LIFETIME_OPTION.md) now makes the decision
reviewable: additive opt-in owner, one attached channel, fresh generation
resources, explicit teardown and complete session/bin propagation. It is not
approved or implemented and cannot qualify the unchanged default profile.
