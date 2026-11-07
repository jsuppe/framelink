# Linux CPU stage diagnostics — failed target delivery

Four five-second cases: both processes exited0 and reachedEOS, all240000PCM samples arrived. This does NOT mean video passed. Independent expected counts show NV121080p23/300, BGRA1080p8/300, NV124K5/150, BGRA4K11/150; every received video frame missed the17/34ms diagnostic. All16trace footers show zero loss/rejected threads. Preserve these as failed unsupported target profiles forM6.8, not successful benchmarks.

NV12 consumer map means .0025/.0067ms, allocation .0037/.0119ms, zero-fill plus copy231.28/1148.30ms at1080p/4K. Clearly the slow payload-read path is outside map/unmap calls. Scope includes destination fill/first-touch; no isolated root cause for every byte is claimed. BGRA consumer srcCreate ~644/474ms includes payload readback and frame acquisition; no isolated BGRA copy-duration scope currently exists, so do not label it memcpy time. Bimodal format/size behavior is retained, not averaged away.

Producer pushed300/150buffers but far fewer were transported; sinkRender means across all calls mix successful copies and bounded ring-full handling. Do not equate pushed count to transported count or infer successful-upload cost from that aggregate. M6.8 will compare allocation/cache paths with pixel/visibility/lifetime validation. These diagnostic runs do not change public overload semantics.
