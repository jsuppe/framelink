# Decoder replay-capable revision b247e5d

- [Build and on-demand tiny smoke](build-smoke/README.md): 9/9 focused tests and
  60 distinct frames / 96,000 PCM smoke PASS.
- [First 4K30 replay control](4k30-control/README.md): tape preparation and
  trace-off timing PASS; trace-on functional/trace checks PASS but timing FAIL
  on two frames. **Overall pair FAILED; no endurance run.**

The first replay control does not establish a successful matched 4K pair or
sustained-rate qualification. The encoded tape remains external with its exact
hash/provenance archived. Raw evidence and original-byte manifests are retained
in each subdirectory; Git-filtered blob hashes, if added, are a separate domain.
