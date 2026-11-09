# Explicit Windows render-producer profile and real decoder A/V

Base clean source `a6b05da69f89fbceb69f5ec35ec3bdf550334462`; changed source hashes in final-profiles/status.json. Production changes are confined to Windows-only local helper and candidate-validation branches. No public symbol, struct, wire version, overload or fallback default changes; no deferred Linux/Android campaigns resumed.

## Contract correction

A decoder/converter that renders into the issued ring must explicitly request rendering, not merely sampling. The initial pilot completed with a sampling-only declaration but is **not** the qualifying contract evidence. Correcting the declaration to GPU_SAMPLE|GPU_RENDER failed at candidate acceptance before the fix (before-fix/decoder-diagnostic.log). The local helper and both bins now additionally accept exactly that producer usage mask, with operations=0, alongside the existing GPU_SAMPLE profile. The helper requests FL_GPU_RENDER on the actual channel. Required usage must evaluate successfully; CPU usage, render-only, other unsupported masks, and undeclared operations still reject. Peers authenticate the exact full candidate before allocation; no downgrade is added.

The existing Windows allocator creates shader-resource/render-target rings; the real producer checks those actual texture flags. Producer-only capability does not make private decoder NV12 arrays shareable. The existing same-device ordering, bounded completion, retained references and strict foreign-allocation rejection remain intact.

## Verification

- Production build and **5/5** focused tests passed: native producer, native source, policy generation, session transport and session context. Bin controls now include sample+render acceptance and CPU/write/render-only rejection, immutable configuration and bounded cancellation.
- Final CLI-only expansion rebuilt and passed **four** independent automatic authenticated A/V sessions: native/readback receiver profiles, each with both EOS orders. Each session decodes eight real 320x240 H.264 frames, explicitly GPU color-converts/scales to an issued 64x32 RGBA ring, and transmits 12,800 synthetic stereo PCM frames. Totals: **32 video frames / 51,200 PCM frames**.
- Actual decoder PTS origin3600000000000000ns is explicitly subtracted once upstream; relative decoder PTS/duration and absent DTS are checked. Synthetic PCM shares this normalized timeline with an intentional37ms video offset. This is not original absolute-PTS preservation or decoded-audio qualification.
- Converter uses the exact issued GstD3D11Device wrapper and completes its GPU work before releasing decoder storage. Pixel-oracle readbacks verify black RGB/opaque alpha at production and all exact bytes at reception. PCM bytes, original producer allocation, common A/V mapping, ordered aggregate EOS and held native references across complete teardown all pass.

`--decoder-av-diagnostic` is opt-in because installed x264/h264parse/d3d11h264dec are required. No fake completion-contract declaration is made on behalf of the stock decoder; the explicit adapter owns and waits for its conversion work.

## Limits and next action

GPU conversion/scaling and diagnostic readbacks are explicit operations, not a raw-decoder zero-copy claim or hidden transport fallback. Native receiver transport declares zero operations; the readback receiver declares COPY|READBACK. Four tiny same-process sessions are neither separate-process display/two-hop proof nor 1080p60/4K30, physical A/V, latency or lifetime-slope qualification. Original startup1529ms and21.10/84.09KiB/cycle failures remain suspended/Evelyn pending matched mechanism controls; no waiver.

Clean exact-revision build/API/full Windows suite is the next owned check. Then continue two-hop coordinated PCM integration and remaining timing/lifetime repairs. API remains a candidate, D2/D3 unresolved, M6 open, M7 hold. Loom briefing failed due missing chromadb; no subagents used.
