# Exact Windows integration: 461584b

Revision `461584bbf5a6b8b70cec1578d69d35cb4b1ea2ff`, clean isolated Windows source archive/build on <win-host>. Build and API inspection exit0; full CTest **169/169 passed in655.72s**, no skipped tests. Run collected once; diagnostics already registered with CTest were not duplicated.

Archive SHA256: `24457da741884c81e2f88f154f07ae96e5c78f9d788a3799d8a3655883cb3c8a`. Runtime DLL hashes/properties are in `api-runtime.log`; full output in `LastTest.log`, inventory in `test-inventory.log`, commands in `run.py`. Source archive remains in `<linux-home>/dev/evidence/m6-cpu-path/windows-integrated-461584b/` rather than being copied into git.

All119 previous export names retained, nine additive C functions,128 total. Of16 public headers,13 are byte-identical to7b8a274; the three changed headers contain additions/comments, with no existing layout/enum/signature break identified by independent source review. Export-name retention/source review is not an ordinal ABI guarantee. See the adjacent compatibility review and header manifest.

Includes actual three-process native/PCM forwarding, held-old-reader replacement, source-death isolation, explicit automatic FAR5 receiver and transactional unpublished setup. Their focused negative controls remain in their own evidence directories. Baseline-built runtime checks are recorded separately in [compatibility evidence](../windows-compat-461584b/README.md).

API candidate only, **not frozen**. No clean-repeat waiver of historical startup/resource failures; no final rate, physical output or all-platform M6 acceptance. M7 remains held.
