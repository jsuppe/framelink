# D3D11 device lifecycle isolation — PARTIAL

No FrameLink code or transport. Each5s cycle creates a D3D11 device/context and two sharedBGRA1080textures, runs300 paced iterations withFlush, then releases everything. A/raw uses onlyD3D11; B/wrapped additionally creates GstD3D11Device and wraps the same textures using cached measured layouts. 2cycle preflights and100cycles perarm passed. Every final deviceRelease reported0 references in both arms (debug count, not comprehensive driver-memory proof).

After5warmup privatecommit fitted slopes: raw31.225KB/cycle,wrapped28.210KB/cycle, both exceed the existing16KB/cycle transport gate. Handles ended235 in both. Growth therefore occurs in a minimal device/texture lifecycle without FrameLink or GStreamer wrapping; a FrameLink-only leak is not necessary to produce the symptom. It does not prove all original21.10KB/cycle growth has exactly this cause, nor waive acceptance.

Next matched control holds a single exclusively owned D3D11 device/context across cycles while textures and Gst wrappers still recreate. This tests device churn vs per-cycle texture/wrapper churn before considering any production change. Generic process-global device sharing is unsafe without exclusive ownership, adapter/device-removal and teardown guarantees; no cache is introduced into production here.
