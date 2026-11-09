# Broker-bound receiver-bin memory policy

`framelinkavsrc` now forwards `apply-memory-policy(GstFrameLinkMemoryPolicy*)`
to its adopted video source. Configure all properties in NULL, adopt the channel,
then bind the authenticated policy. Endpoint zero is **this receiver**: swap both
requirements and paths from the producer-ordered authenticated candidate. The
caller still validates actual resources; the action itself is not authentication.
A rejected bind does not freeze the bin. A successful bind freezes bin properties
and rejects rebinding; reconnect requires a new bin/session. Shared-CPU, audio-only,
and automatic local-discovery modes are not eligible. Existing unbound behavior
and overload/fallback defaults are unchanged.

The Windows native-producer A/V integration now binds both bins without receiver
child access. It checks exact pixels/PCM, original 37ms offset, both EOS orders,
empty EOS and peer death; the receiver uses explicitly allowed COPY|READBACK,
not end-to-end zero-copy. Added receiver-bin generation regressions reject both
pre-start and live pool replacement; property changes cannot rebuild video or
switch to shared-CPU/native output after binding. Existing thin-source and sink
generation and native-reader completion tests remain in the same suite.

Validation logs and source hashes accompany this file. This is an additive API
candidate, not an API freeze, automatic native discovery, or M6 closure. D2/D3,
Windows original startup/lifetime gates and deferred platform acceptance remain
open; M7 remains on hold.

Validated on the source hashes recorded here: **Windows4/4, Linux3/3, Android3/3**, both Android ABI builds. Windows native AV uses receiver-bin policy binding in all four cases. No deferred performance campaign was run.
