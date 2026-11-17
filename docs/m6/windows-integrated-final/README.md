# Final M6 Windows integrated qualification — `6ee0f86e` (2026-11-17 03:32–03:45 UTC)

Run `runs/final-6ee0f86e`. Controller state is `finished`, all local checks pass, and the host was idle before and after. 45 collected files were verified against the uploaded manifest. Profile: a fresh Release build of every target from the `git archive` of `6ee0f86e7efd636ba8802663d9eb1bc4c3e19d5b`. The archive sha256 is in `config.json`; the tarball is not committed.

| Check | Result |
|---|---|
| Full serial CTest (no filter, no retries) | **179/179 pass**, 0 skipped, 671.07 s. The previous 178 Windows tests plus `device_owner_win` |
| Exports | **137 = 128 baseline (`a50841a`) + 9 owner**: framelink.dll 88, framelink_gst_session.dll 49. Nothing removed, nothing unexpected |
| GStreamer | all four elements inspect; `set-local-d3d11-device-owner` present on `framelinkavsrc` and `framelinkavsink` |
| C11 | a C translation unit including `framelink_d3d11.h` + `framelink_session_transport.h` (twice) compiles |
| Old clients | 3/3 unchanged `7b8a274`-built clients (`fl_frame_metadata`, `fl_gst_native_epoch_c_api`, `fl_read_lease_d3d11`): imports and runs pass against the new DLLs |

New DLL hashes: framelink.dll `f6b38ec8…7fbc44`, framelink_gst_session.dll `6aba39e1…79f909`.

**Relation to the lifecycle evidence:** the option B lifecycle runs used binaries built from `d164742`. Between `d164742` and `6ee0f86e`, the only source change under `src/ gst/ include/ test/ tools/` and CMake is the header-only `flD3D11DeviceOwner` typedef guard (6 added lines). That isn't byte-identical rebuild evidence, but it is a reviewed, non-functional delta.

Harness: adapted from `windows-integrated-a50841a`. Only the references, the public-header allowlist, the owner-signal check and the C11 step changed.

**Commit ids:** this run was made at local commit `6ee0f86e7efd636ba8802663d9eb1bc4c3e19d5b`. Before publication the message of an ancestor commit was reworded, with no content change, so the published id of the same tree is `eb3505a2`. The lifecycle build commit `d164742` is published as `f4185511`. Ids inside the `runs/final-6ee0f86e` evidence files are the local ones.
