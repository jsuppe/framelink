# Approved renderer baseline recovered — no new qualification

The source-selection dependency recorded in7a637e4 is **resolved**, not a product
choice for Jon. The prior lookup was incomplete: plans/M5_STATUS.md identifies
approved renderer PR13 merge9f74b9d1fae7e13d520bc5d4ae9827ac568704f6; GitHub's
PR13 metadata independently confirms merged=true and final head
ecda87122b87436a1e049afd0c5de270be4b5828. Parent-approved performance baseline
151ecd6 and final CI/merge scope are retained in M5_STATUS/M5_PERFORMANCE_STATUS.
The earlier /tmp/vkrender-m5 checkout no longer exists; that did not erase its
approved remote commit or create a new architecture decision.

## Evidence

- Clean detached source recovered at
  <linux-home>/dev/vkrender-m6-renderer-baseline from the exact approved merge.
- Git tree e84a0ab258f0d5fe94cc25c2ffc3dc0f5f6e462a;1624 tracked file SHA256 pins.
- Source tar (external evidence,88299520bytes) SHA256
  f01c9cbafe7a50b54d8538c71173f1708033852b72b7208a533b631de8b4f4ac.
- Compared137 files under historical Windows renderer/common/client/decoder/
  cmake/test plus root CMakeLists:136 byte-identical to the approved merge,
  including av_input.h, av_schedule.h and renderer implementation. Zero
  historical-only files in those enumerated directories.
- Sole difference is four CMake lines setting av_clock_contract TIMEOUT30,
  matching the final CI correction documented at M5 closure. No renderer runtime
  difference in the enumerated source. This does not fingerprint every existing
  build input/dependency or freshly qualify the historical binary.
- Unrelated local983d1ea dirty tree, Windowsed03cb8 tree and barec71e013 untouched.
  No build, test, media capture, production edit, default change or API freeze.

PR: https://github.com/jsuppe/vkrender/pull/13

## Next routine isolated integration step — prepared, not executed

1. Use renderer9f74b9d and FrameLinka50841aad44fbc9431234a4f326bad036d193bf5
   source archives in fresh Windows directories. Verify tar and extracted source
   manifests before configuration. Do not reuse or overwrite old build trees.
2. Read Loom context for any intended implementation file before edits. Configure
   renderer against that explicit FrameLink directory with GST and samples on;
   use the established GStreamer runtime PATH and no MSBuild node reuse.
   Build only vkrenderer and relevant portable scheduler/clock targets first;
   retain exact command/output and binary hashes. No full-suite rerun implied.
3. Review actual integration before changing anything: AvInput uses
   av::runPlayback and FrameLink samples/av_playback.h, whose explicit final-
   playback latency default remains100ms. Ring12/pending6 are the approved M5
   ownership/scheduling profile. These are not the M6 transport-only2/1 profile.
   Do not silently set every final sink unsynchronized or remove needed ownership.
4. Determine the smallest explicit primary-profile integration change preserving
   common A/V mapping, bounded setup, real completion/retirement and final-stage
   scheduling. Current same-allocation relay/decoder proof does not automatically
   make this older playback adapter a negotiated M6 native receiver.
5. Only after the integration is concrete, run newly affected focused controls
   and bounded joined source/GPU/presentation-submission checks. Keep physical
   scanout/acoustic synchronization excluded; no speculative1800s rerun.

The next implementation owner is qualification_recovery, serially. Parent remains
conversation/coordination only. Device-owner option, D2/D3 and suspended timing /
resource failures remain untouched. M6 open; M7 held.
