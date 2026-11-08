# Explicit CPU sink and video pipeline roundtrip

`framelinksink shared-cpu=true` claims the coordinator's producer CPU channel,
requires actual negotiated COPY plus strict metadata/EOS, and rejects native
adoption, native-memory mode and slot-set discovery. Like the initial CPU source,
it currently rejects audio tracks/separate event streams. Existing default and
native sink paths remain unchanged. It does not advertise general A/V support.

The explicitly selected CPU mode preserves every accepted frame: no latest-wins
replacement, hidden drop or fill-before-delivery. Before exceeding the negotiated
videoQueueFrames it collects an exact outstanding release; slot reuse may collect
further releases. These waits are bounded by the broker's channel I/O timeout;
peer loss/timeout fails instead of silently recycling. This is a selected bounded
ownership-wait path, not a resolution/change of the unresolved general D2 default.
Legacy buffer-wait-ms/drop controls do not select this path's broker I/O deadline.

CAPS must exactly match session geometry/format and use SystemMemory. Input must
have one SystemMemory block: native GPU memory and multi-block mapping reject
before access, avoiding GStreamer merge copies. GstVideoFrame mapping supports
standard padded input; logical rows copy into the separately validated pool
strides/planes. Unsupported negative strides reject. PTS/DTS/duration convert via
the existing forward TIME segment contract, with generation and overflow checks.
No partially copied frame is submitted; writes are cancelled on error. Actual
bytes are traced, and CPU render failures count as dropped/error-accounted inputs.

EOS is sent after all submissions, then the sink drains every outstanding read
lease before reporting local EOS. A consumer-held final frame therefore prevents
successful local completion but never unsafe reuse. Unlock/flush/teardown cancels
the session and shuts down a blocked release wait; new generation/channel is
required afterward. Same-generation pause/flush resume remains unqualified.

## Evidence and limits

Two actual appsrc→CPU sink / CPU source→appsink pipelines delivered eight BGRA and
eight NV12 frames with every logical pixel and mapped PTS/DTS/duration correct,
including padded18-pixel rows and non-contiguous NV12 pool planes. Tests reject
missing actual COPY, incompatible CAPS, overflowing duration and multi-memory
inputs. Empty EOS succeeds. A raw consumer holds the final frame and receives
the ordered EOS marker: local sink EOS stays blocked for the50ms observation;
teardown interrupts its60-second receive deadline within the frozen1-second gate.

Linux26/26 selected tests and Android14 runtime binaries pass; both Android ABI
builds pass.100 repeated roundtrip runs pass. The final error-accounting-only
correction was rebuilt on both ABIs and rerun on Linux/Android; broader results
precede that localized correction. Final trace: sink17 copies/13,824bytes,
source16 copies/12,672bytes, allCOPY; difference is the raw-consumer held-frame
case. Two rejected render inputs have explicit error/drop records. This is
functional video coverage, not sustained throughput, A/V or full final-head M6.

Development evidence: an initial terminal assertion failed without case labels;
its cause is not retroactively assigned. Added case diagnostics exposed that the
missing-first-PTS negative was unreliable because appsrc can supply it; replaced
with deterministic duration overflow. Current case-labelled100-repeat passes are
retained, not a claim proving the original unlabeled failure's cause. The final
validation controller's post-test hash step referenced a nonexistent copied
filename; all tests/builds had passed. Corrected the script and regenerated exact
source hashes without rerunning unchanged tests. Commands/logs/hashes are in
`shared-cpu-gst-sink/`.

Next: coordinator/bin selection and A/V/event/flush integration, authenticated
rendezvous provisioning, Windows allocation and target-rate/endurance gates.
