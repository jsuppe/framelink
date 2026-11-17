# Native pause diagnostics — two failed captures

These immutable captures refine the [initial failure](../initial-failure-9ea51d4/README.md).
They do not contain validation of the later fix.

| Capture / source revision | Result |
| --- | --- |
| [1c74e90](1c74e90/LastTest.log), `1c74e904706fa5202413402565678bacd686548a` | Build PASS; pause FAIL at line 447, `collected==FL_OK`, 2.35s. Paused-cancel PASS (97.394ms propagation); existing bin PASS (1.14s). |
| [db43f54](db43f54/LastTest.log), `db43f54377cf3a6369c96a073ab6e2da00252646` | Build PASS; pause FAIL at line 453, 2.34s, with explicit lease/collection diagnostics below. Paused-cancel PASS (101.425ms propagation); existing bin PASS (1.12s). |

Both focused runs finished 2/3 PASS (CTest exit 8); decoder smoke was **not run**.
Both final idle receipts show no active runtime processes. Failed controller
statuses are preserved; packaging and collection succeeded.

## Confirmed cause and pending repair

The db43f54 trace reports final memory references **2 before NULL → 1 after NULL**,
then **final unref returned**. Its next diagnostic is:

```text
collect=2 pending=0 inputQuery=0 inputGeneration=2
outputQuery=0 outputGeneration=2 childExit=259
```

`2` is `FL_DISCONNECTED`; both channel queries succeeded, generations remained 2,
and `259` is Windows `STILL_ACTIVE`. Thus the observed failure is not an unreturned
final unref or an outstanding upstream lease: pending work was already zero.
Source inspection confirms `NativeRelay::collect` could continue acquiring after
all missing reservations had been recovered, probing a fully reserved ring after
the completed reader closed and reporting disconnect.

Fix `462fa5f192cd9b3347c798afa8108b9358716f7b` bounds collection by the number of
missing reservations; existing generation checks and required acquisitions remain.
**At archival handoff the fix's Windows eight-test/decoder validation was pending.**
No passing repair result is established by these two diagnostic failures; any later
result must be archived separately.

## Provenance and original bytes

Source run directories under `windows-native-ready-batch-prepared/runs/`:

- `pause-diag-20261112t0804z` (the actual capture ran about 08:03 UTC).
- `pause-lease-diag-20261112t0804z` (about 08:04 UTC).

Each subdirectory contains 17 copied raw/manifest/controller files plus
`original-byte-sha256.json`. Status preserves the five exact built-binary pins;
LastTest preserves commands, diagnostics and timings. Build/configuration,
inventory/host, focused results and final-idle receipts remain intact.
No source tar, executable, discovery credential or unrelated payload is included.

Hash domains:

1. `artifact-sha256.json`: original uploaded-input hashes (including source tar and
   scripts not included in this archive).
2. `collected-sha256.json`: original Windows manifest; all **15 entries per capture**
   were verified against source files before copying.
3. `original-byte-sha256.json`: hashes of all **17 copied files per capture**,
   excluding itself and this README. Original CRLF/BOM bytes are preserved.

All original hashes refer to pre-filter bytes, **not Git blobs**. Repository clean
filters may rewrite environment paths during staging. Retain these manifests
unchanged and keep any post-filter Git-blob hashes in a separate manifest/domain.
