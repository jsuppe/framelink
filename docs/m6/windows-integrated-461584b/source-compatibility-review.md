# Source compatibility review: 7b8a274 to 461584b

Independent native reviewer: no public struct, enum, existing function signature or serialized session layout break identified. Nine additive exported C functions (five backend bounded-open/snapshot, four local native session setup) and opaque flD3D11PoolSnapshot. FAR1-FAR4 formats remain unchanged; FAR5 explicit/no fallback. Bounded producer setup requires negotiated pull-handle capability and rejects older peers with FL_VERSION_MISMATCH; legacy open protocol remains available. CPU pipe authentication transcript retains its original trailing NUL.

Behavioral migration notes:
- Explicit unavailable D3D adapter now rejects with FL_ADAPTER_MISMATCH, no silent default-GPU fallback. Select a valid adapter or explicitly request default selection.
- Windows legacy producer-open failure now clears its output after valid name/output argument checks; a null name returns before clearing it. Check the result before using it; never use an out parameter as storage for a live channel.
- Snapshots must be retained for the whole relay graph, not just setup. They pin pool/epoch, preventing geometry replacement and new producer attachment until released; their lifetime does not substitute for a read lease.
- New FAR5 receiver action is NULL-only, copied/frozen, A/V plus explicit pool; FAR4 action and properties/defaults unchanged. Fresh bin per reconnect.
- setupLocalNativeRelay is prepublication setup only: no readiness, media adapter or live-graph teardown.
- endPublication permits authenticated terminal publication while native readers remain; separate genuine native drain remains mandatory; finish remains strict.

Source review is not old-binary, ordinal-import, runtime dependency or all-platform proof. Integrated run and separate baseline-built client checks remain necessary. No API freeze/M6 closure claim.
