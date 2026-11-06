# Android native regression gate — b5236a4

All 72 CTest-defined native cases passed on the TCL8483A arm64 device; no skips. Full arm64 and x86_64 API29 builds passed (x86_64 completed149 build steps). x86_64 runtime execution is not claimed. Commands were translated to adb, static plugin arguments and device paths; CTest environment and timeout/skip properties were retained. This is native same-UID shell coverage, not a fresh signed cross-UID APK qualification or physical-output claim.

The initial host wrapper incorrectly backgrounded its working-directory setup for roundtrip: consumer exited127 before running. Grouping the roundtrip shell body repaired the wrapper; the rerun verified20frames with zero mismatches. Earlier passing cases were retained; failed launch log remains in the persistent evidence archive. No production change was made for this wrapper error.

Archive: android-regressions under persistent M6 soak evidence. Runtime b5236a4; later commits are documentation only. M6 remains open for sustained matrix and subsequent milestones tasks.
