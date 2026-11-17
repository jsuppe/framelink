# Baseline-built client compatibility: 7b8a274 → 461584b

**Three baseline-built clients passed unchanged against both old and new runtime DLLs.** Fresh baseline archive/build at `7b8a2741416a59be1fbd66df13a4e88e95bb9bd9`; candidate DLLs are the exact integrated `461584bbf5a6b8b70cec1578d69d35cb4b1ea2ff` binaries. Build, import inspection, baseline control and candidate-runtime execution all exit0.

| Unchanged baseline client | Runtime behavior exercised |
| --- | --- |
| `fl_gst_native_epoch_c_api` | C11 public-header/linkage and existing native-epoch entrypoint |
| `fl_frame_metadata` | Exact metadata, optional absence/reuse, required publication and incompatible-peer rejection |
| `fl_read_lease_d3d11` | Separate-process shared-allocation GPU read, fence retirement and all-pixel verification |

`hash-verification.json` independently compares the two copies of every client and matches candidate DLL hashes to the integrated run. `baseline-imports.log` records named FrameLink imports; this is not a stable ordinal-import promise. Expected incompatible-peer error logs are negative controls, not execution failures.

These tests are bounded old-client behavioral checks, not universal ABI, all older release, all-platform or clean-install qualification. Public-header/source review and full169-test candidate evidence are [separate](../windows-integrated-461584b/README.md). API remains **not frozen**; M6 remains open and M7 held.
