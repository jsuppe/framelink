# M5 platform capability / evidence matrix

Final integration review, 2026-11-01, source `ec4d546` plus the corrected external
failure harness. A platform pass does not imply another platform passed. M5a
memory negotiation/native GStreamer allocation is excluded. M5 completion means
the qualified scope below, not unrestricted platform parity.

| Capability | Linux | Windows | Android |
|---|---|---|---|
| Authenticated separate-process A/V | M4/M5 application tests | M4/M5 and external decoded pipeline | Signed broker, separate producer process, different-UID client |
| External decoded GStreamer input | BGRA/NV12 MJPEG + PCM AVI, bytes/PTS/duration verified | Same fixture on Caspar | Not qualified; generated source only |
| Original PCM/video PTS | Offset and reset/drift negative controls | M4 offsets and M5 decoded fixture | Every generated pixel/PCM sample and original PTS verified cross-UID |
| Native shared video transport | dma-buf path | D3D11 shared texture/fences | Delegated AHardwareBuffer/connected-FD ownership |
| GPU-only decoder-to-renderer pipeline | Not promised; M5a | Not promised; M5a | Not supported by this CPU GStreamer/Surface example; M5a |
| Platform audio API | PulseAudio API on dedicated null sink | WASAPI, finite/native A/V qualification | OpenSLES ProducerMapped with guarded resampling |
| Video delivery/presentation | Existing native and virtual-display tests | Native BGRA/NV12 1080p60, four 70s cases; traced 4200/4200 through GPU completion and API-accepted present | 160x90/30fps; BGRA 300/300 and NV12 450/450 through accepted Surface posts |
| Cross-application ownership | Same-user private rendezvous | Same-user ACL/private rendezvous | Signature permission, lease authentication, replacement/revoke, independent owner/peer death and fresh restart |
| Activity/focus lifecycle | N/A | N/A | Background/focus/surface loss cancels both tracks; explicit new-session reopen |
| Missing endpoint/plugin/media; incompatible raw caps | Corrected four-case negative matrix passed | Same four-case matrix passed on Caspar | Wrong signer/key, unsupported format and invalid bounds passed; desktop plugin/file cases not applicable to packaged source |
| Background playback | Process owns session | Process owns session | Unsupported in foreground-Activity sample; cancel/reopen is supported |
| Audio route removal | Missing Pulse server fails; missing device can fall back | Physical route change not qualified | Stop/reopen policy implemented; actual route-unplug qualification outstanding |
| Suspend/resume | Seamless resume not qualified/supported | Same | Same; explicit stop/new-session restart only |
| Physical speaker/display alignment | Unmeasured; no physical audio endpoint exposed | Unmeasured | Unmeasured |

## Evidence and important exclusions

- `SUSTAINED_PARENT_REVIEW.md` and renderer sustained-qualification evidence:
  Windows steady 59.993fps, complete traced identities; untraced aggregate counts
  are not per-identity proof. No physical scanout, cold display-wake or acoustic
  timing claim. These accepted captures are not new Nov1 Windows performance runs.
- `external-pipeline-linux/`, `external-pipeline-windows/`,
  `../M5_WINDOWS_PIPELINE.md`: decoded source handoff and reproducible guide.
- `ANDROID_DEPLOYMENT.md`, `connected-video-20261101/`, `crossapp-20261101/`,
  `audio-alignment-20261101/`: signed cross-UID integration and final 20/20 device
  matrix, dual-ABI builds, complete original PCM/PTS and saved sink-call audit.
  Android runtime is one arm64 API35 device, not Pi or x86 Cuttlefish qualification.
- Android explicit audio-master diagnostic remains **unqualified**: late device
  clock startup can stall its synchronous Surface callback and lose video.
  ProducerMapped is the qualified example default; no silent clock fallback.
  The OpenSLES negative-forward-span defect is repaired with a real sink regression;
  a resync can alter hardware output phase, not original transported timestamps.
- `final-integration-20261101/`: current Linux/Windows negative tests verify the
  intended error, bounded exit, no live endpoint and reaped children. Historical
  `linux-external-failures/missing-media` failed while linking; it did **not** prove
  missing-file handling. The corrected fixture reaches the file-open failure.
  `.owner.lock` persistence is intentional; cleanup does not prove GPU-driver leak
  freedom or long-duration resource stability (M6).
- `TRANSPORT_DECISION.md`: retain serialized/copied GDP/GIO PCM. No measured unmet
  requirement justifies a new PCM transport. No quantitative latency SLA is claimed.
- Route unplug, suspend, optical/acoustic measurement and additional Android
  hardware remain explicit qualification gaps. They are not advertised as seamless
  supported behavior. Native GStreamer allocation/zero-copy belongs to M5a;
  sustained soak/restart/resource release gates belong to M6/M7.

Reproduce desktop deployment negatives (Windows: point to `build/Release`):

```
python test/m5_external_failures.py --build build --output /tmp/m5-failures
```
