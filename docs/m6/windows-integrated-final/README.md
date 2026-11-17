# Final M6 Windows integrated qualification — `060c54e4` (2026-11-17 03:54–04:06 UTC)

**Current run: `runs/final-060c54e4`**, at published commit `060c54e41bcbfe7ca8dbdef89e6e74fb2930935c`. Controller state is `finished`, all local checks pass, and the host was idle before and after. Profile: a fresh Release build of every target from the `git archive` of that commit (archive sha256 in `config.json`; the tarball is not committed). Hosted CI also passes on this commit for Linux and Windows (PR #10).

| Check | Result |
|---|---|
| Full serial CTest (no filter, no retries) | **180/180 pass**, 0 skipped, 674.60 s. The 178 tests of `a50841a` plus `device_owner_win` and `device_owner_faults_win` |
| Exports | **137 = 128 baseline (`a50841a`) + 9 owner**: framelink.dll 88, framelink_gst_session.dll 49. Nothing removed, nothing unexpected |
| GStreamer | all four elements inspect; `set-local-d3d11-device-owner` present on `framelinkavsrc` and `framelinkavsink` |
| C11 | a C translation unit including `framelink_d3d11.h` + `framelink_session_transport.h` (twice) compiles |
| Old clients | 3/3 unchanged `7b8a274`-built clients: imports and runs pass against the new DLLs |

DLL hashes: framelink.dll `14b84733…de7403`, framelink_gst_session.dll `9f3ab33d…298b41d`.

**Relation to the lifecycle evidence:** the option B lifecycle runs used binaries built from `f4185511` (local id `d164742`). Since then the only changes under `src/ gst/ include/` are the header-only `flD3D11DeviceOwner` typedef guard (6 lines); `test/` and CMake gained the fault test. That isn't byte-identical rebuild evidence, but it is a reviewed, non-functional delta for the production DLLs.

Harness: adapted from `windows-integrated-a50841a`. Only the references, the public-header allowlist, the owner-signal check and the C11 step changed.

## Earlier run (superseded)
`runs/final-6ee0f86e`, 2026-11-17 03:32–03:45 UTC, before the fault test was added: 179/179, same export, C11 and old-client results.

**Commit ids:** this run was made at local commit `6ee0f86e7efd636ba8802663d9eb1bc4c3e19d5b`. Before publication the message of an ancestor commit was reworded, with no content change, so the published id of the same tree is `eb3505a2`. The lifecycle build commit `d164742` is published as `f4185511`. Ids inside the `runs/final-6ee0f86e` evidence files are the local ones.
