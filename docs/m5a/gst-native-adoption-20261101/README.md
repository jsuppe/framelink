# M5a.7d native source adoption

`adopt-channel` now accepts a transferred native consumer only with
FL_DEFER_RELEASE and without FL_GPU_SYNC (this source owns the normal readiness
wait). Local consumer flags are queried through additive flQueryConsumerFlags;
producer/null queries reject without modifying output. Arrival-age policy also
requires an adopted channel with arrival tracking.

Linux native/adoption + reader tests **2/2**, Windows native/adoption, copy
negative and readers **3/3**; Android native/adoption + readers pass, both ABI
builds pass. Each adoption test first rejects a non-deferred channel and verifies
caller ownership remains intact, then accepts a deferred channel and completes
original storage/pixels/PTS, bounded reuse and lifetime-after-stop checks.

This validates the ownership seam used by SessionProvider/broker clients, not a
new signed cross-UID native-Gst application or remote memory-policy handshake.
Existing broker authorization/revocation obligations remain mandatory.
