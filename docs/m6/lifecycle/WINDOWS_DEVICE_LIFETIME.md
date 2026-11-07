# Windows lifecycle resource acceptance — suspended, not waived

## Evidence and scope
Both100-cycle directions deliver exactcounts but fail the frozen16KB/cycle private-commit slope: persistentconsumer21.10, persistentproducer84.09. Minimal device creation/destruction reproduces31.225 without FrameLink or GStreamer wrapping; wrapped28.210; exclusive persistentdevice -0.384; DXGI Trim35.761. Actual final deviceRelease returns0 in churn probes. These controls establish a driver/device-lifecycle contribution, not complete attribution of either production slope. ReturningCOMrefs tozero is not proof of zero driver-retained allocation.

## Why no automatic cache patch
Existing API creates independently owned devices/channels, allows borrowed native device references to be retained, and supports unload after close. A process-global shared device changes queue/fence ordering across live channels. Inferring exclusive ownership from transient COMreference counts is unsafe with external references. Keeping GPU objects until DLL static destruction risks loader-lock teardown; intentionally leaking them would hide the lifecycle contract failure.

An explicit application-owned device-lifetime API could avoid device churn while preserving a defined shutdown point. It requires channel/adapter matching, removed-device refusal, external lease ownership, queue ordering and integration through the session/GStreamer APIs. An opt-in result would not qualify the unchanged default fresh-device profile. No API change or benchmark substitution has been made.

## Status and resume condition
M6.3a Windows resource acceptance remains suspended with original limits and failures retained. Evelyn owns the integration/measurement follow-up; the device/driver lifecycle behavior is an external platform dependency. Resume default-profile acceptance after a driver/backend change with matched100-cycle evidence, or an explicitly approved supported device-lifetime contract with its own ownership tests and complete measurements. Jon owns any product-contract/acceptance decision; none has been requested or assumed here. No immediate user action is needed to continue ready in-milestone work.

Next ready task M6.3b: verify outstanding-native-lease, generation, EOS/pause and bounded-cancellation coverage. Linux/Android3a passes do not close Windows or allM6.
