# Windows native-session pipeline epoch prerequisite

Implementation is the commit introducing this document, based on `5863056`.

Adds `fl_gst_session_open_local_native_epoch` and public C callback type
`FlGstNativeEpochFunc`. This lets the future automatic native bin pass its
already-selected pipeline base time into authenticated session setup rather
than introducing a second epoch. The callback runs on the setup worker after
the control connection, before negotiation. It must not block; arguments remain
caller-owned for the duration of setup. Negative epoch rejects setup; consumer
callbacks are ignored, so only the authenticated producer selects the epoch.
The original function delegates with a null callback and retains its now+200 ms
behavior. Struct layouts, policy/wire formats, overload and fallback defaults
are unchanged. Automatic native bin wiring is still pending.

## Verification

- Windows **6/6** focused tests passed, **15.71 s**, six exact source overlays
  on the previously clean `34a391d` tree (source hashes and commands retained).
- Existing four broker A/V cases and three default-epoch local cases retained.
  Three new local cases use a producer epoch one second in the past: both EOS
  orders and empty stream. Both endpoint configurations match that exact epoch;
  producer callback runs once and receiver callback never runs. Existing exact
  original D3D allocation identity, pixels, PCM, DTS/duration and 37 ms relative
  A/V mapping checks remain active.
- Eleven local negatives include new invalid-epoch rejection: both endpoints
  fail, no channel/policy output survives, producer reports the epoch error.
- New persistent **C11** test includes only the public header, links and invokes
  the exported function, and verifies pre-negotiation invalid-policy rejection
  without invoking the callback. Initial build failure due to referencing the
  internal-only callback typedef is retained in `initial-header-failure`; fixed
  by the public type, not by leaking the internal header into consumers.
- Runtime property/DLL inspection passes. All **118** previous export names are
  retained, with the one new epoch function (**119** current). Symbol retention
  is not a complete ABI proof. Clean exact integrated build/suite follows.
- Changes are Windows-only, including CMake selection; no Linux/Android campaign
  was resumed. Loom pre-edit briefing failed because `chromadb` is unavailable.

D2/D3 and resource/rate acceptance gates remain open. Original uninjected
`548fe19` 1529 ms startup failure remains suspended with Evelyn as owner; no
clean-repeat waiver. API not frozen, M6 open, maintenance hold before M7.
