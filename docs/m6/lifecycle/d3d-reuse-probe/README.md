# Exclusive D3D11 device reuse control — validated mechanism, not production fix

Same100x5s/2BGRA1080textures/Gstcachedwrappers as prior probe; one device/context kept exclusively alive across cycles. Textures and wrappers still recreate. Privatecommit slope-0.384KB/cycle after5warmup, compared with28.210 wrapped per-cycle device creation and31.225 raw device creation. Final actual deviceRelease at exit returned0; per-cycle CSV device_release_refs=0 is a placeholder because no release occurs within reuse cycles.

This supports device churn as a reproducible driver-allocation cost. It does not authorize a shared global device: inter-channel fences/context state, outstanding external COM references, adapter changes, removal and DLL unload all need safe lifetime handling. No production change made.

Next narrow alternative: DXGI device Trim before releasing a newly created device each cycle, otherwise identical. Test whether explicit driver-cache release avoids growth without introducing a persistent device/cache or shared channel ownership.
