# M5 platform capability / evidence matrix

Snapshot: source `210bb53` plus independent external-failure harness. A pass in one
column must not be read as parity in another. M5a memory negotiation is excluded.

| Capability | Linux | Windows | Android |
|---|---|---|---|
| Authenticated separate-process A/V session | M4 + M5 app tests | M4 + external decoded pipeline | Same APK, separate producer process only |
| External decoded GStreamer source | BGRA/NV12 MJPEG+PCM AVI; bytes/PTS/duration verified | Same fixture passed on Caspar | Not qualified |
| PCM offset preservation | 37 ms/123.456789 ms app cases, negative reset rejected | Existing M4/M5 evidence | Receiver completion/focus lifecycle, not external source offset matrix |
| GPU-native shared video | Existing native transport/renderer path | Existing native transport/renderer path | Existing Binder video API separate from A/V receiver |
| GPU-native decoder-to-renderer zero-copy | Not promised; M5a | Not promised; M5a | Not promised; CPU Surface qualification path |
| Actual platform audio API | pulsesink exercised on dedicated null sink | M4 Windows clock/playback tests; see platform evidence | OpenSLES selected, actual APK execution |
| Physical speaker/display alignment | Not measured; no physical audio exposed | Not measured by decoded fixture | Not measured |
| Activity lifecycle/audio focus | Not applicable | Not applicable | Background cancellation, focus callback, reopen, Surface recreation passed |
| Cross-application session ownership | Same-user private rendezvous | Same-user ACL/private rendezvous | Unexported same-app services only; cross-UID broker still pending |
| Missing endpoint/plugin/media, invalid raw caps | Four bounded failure cases passed; processes reaped and endpoint removed | Not yet rerun with independent failure harness | Additional device/format negatives pending |
| Audio-route removal | Absent device may fall back on PipeWire; missing server fails | Hardware route test pending | Stop/reopen policy implemented; actual route test pending |
| Suspend/resume | Not qualified | Not qualified here | Not qualified |
| Background playback | Desktop process remains owner | Desktop process remains owner | Not supported by current foreground-Activity mode; cancel/reopen |

## Evidence mapping
- `TRANSPORT_DECISION.md`: Linux arrival/CPU/copy-cost boundaries and source audit.
- `linux-external-failures/results.json`: missing session endpoint (1.016 s), missing
  plugin/media and incompatible raw caps (each approximately 0.064 s), all fail
  nonzero with no remaining live endpoint and no unreaped children. Persistent
  `.owner.lock` is intentional and not a leak. File/process cleanup is not proof
  of GPU-driver allocation reclamation; no driver leak claim is made.
- `external-pipeline-linux/`, `external-pipeline-windows/`,
  `../M5_WINDOWS_PIPELINE.md`: decoded input payload/timestamp equivalence.
- `ANDROID_DEPLOYMENT.md`, `android-evidence/`: Android supported mode and remaining
  gates. Five lifecycle cases passed on TCL tablet; dual-ABI build is not x86 VM
  runtime qualification. Keep this row updated when Android owner lands new work.

## Remaining milestone decisions/gates
1. Android cross-app authorization and real route/device/suspend coverage remain
   functional work, not erased by same-app APK success. Product-supported background
   cancellation is explicit; persistent background audio would require a new
   foreground-media-service lifecycle contract.
2. External Windows consumer rendering on actual selected endpoints must be
   qualified separately from CPU checksum/fakesink tests.
3. A quantitative latency budget and intended deployment load are not specified.
   Current characterization cannot certify a latency SLA. No evidence warrants
   replacing GStreamer PCM transport; retain it pending an actual measured gap.
4. Linux physical scanout/audio and optical/acoustic alignment remain unqualified;
   transport PTS preservation remains independently verifiable.
5. No blanket all-platform qualification: advertise only the supported rows and
   retain pending tests as pending. Parent owns final milestone closure and review.

Reproduce independent Linux negatives:
```
python3 test/m5_external_failures.py --build build-m5 --output /tmp/m5-failures
```
The harness uses native Linux binary/plugin names intentionally; Windows owner
must port/rerun rather than infer a pass.
