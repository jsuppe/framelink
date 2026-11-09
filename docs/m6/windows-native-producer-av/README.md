# Windows native producer with coordinated PCM

On d9585a8 plus the recorded source hashes, all **4/4** focused Windows tests
passed: native producer, pool policy, shared CPU A/V and A/V bins.
The new four native-producer A/V cases use a broker-configured session,
independently authenticated PCM and event streams, D3D11 allocations obtained
through the producer bin, and an explicitly selected CPU-readback receiving bin.
They verify original producer texture identity, exact pixels/PCM, common mapped
37ms A/V offset, DTS/duration, video-first and audio-first EOS, empty streams,
PCM disconnect failure, and bounded teardown. No native/PCM transport code change
was needed. This is not authenticated native discovery or zero-copy consumption.
No physical playback, sustained-rate or generic-decoder acceptance is inferred.

The change is Windows-test-only (plus its GIO linkage). Public headers, wire
formats, runtime code and defaults are unchanged. No deferred platform campaign
was repeated. Loom briefing unavailable: missing chromadb. D2/D3 and the earlier
Windows startup/lifetime gates remain open; M6 open, M7 hold.
