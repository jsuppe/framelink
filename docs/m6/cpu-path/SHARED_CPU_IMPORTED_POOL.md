# Atomic imported shared-CPU pools (Linux/Android)

`fl::SharedCpuImportedPool` joins session/policy preflight to actual sealed-FD
imports. It remains internal and is not advertised by a public channel factory.
It borrows exactly the descriptor-count FDs; it stages all imports and mappings,
then replaces the active pool only on complete success. Failed later slots do not
partially replace a live pool. The caller retains responsibility for closing its
received ancillary FDs on both success and failure.

Each retained FD supplies actual `(st_dev, st_ino)` identity. Duplicate backing
objects with different logical allocation IDs reject. Consumer imports are reopened
as actual O_RDONLY capabilities even if the sender supplies writable descriptors;
producer imports must successfully map writable. No GPU interop is implied.

Replacement requires the existing session replacement contract (same identities,
strictly newer generation) and unchanged endpoint role. Mappings hold their own
storage owner, and remain readable after replacement. A bounded weak identity
registry rejects reuse of storage from current or still-retained retired imports,
including more than one generation back. Registry entries do not retain storage;
expired entries are pruned at adoption. At 256 live tracked allocations a new pool
returns FL_BUSY, preserving the current pool, until old references are released.
The bound is an internal safety limit, not a new public overload default.

Ownership here is local to a pool instance. It is NOT a substitute for live frame
leases, peer release acknowledgments, producer fences, or cross-process recycle
arbitration. An unrelated importer holding the same object is not discoverable from
this registry. The eventual control protocol must forbid reassigning storage while
any peer has a live lease. Callers externally serialize pool adoption and mapping.

## Evidence

Linux five shared-CPU tests passed. Android arm64 imported-pool and allocation tests
passed; both Android ABI builds passed. Tests include 100 failing last-slot imports
with unchanged FD count, duplicate inode/different ID, stale session generation,
changed session identity, retained pixel visibility across multiple replacements,
read-only producer rejection, move/reuse, 128 fully pinned two-image generations,
bounded FL_BUSY and recovery after release. All FDs return to baseline after scope.
Existing allocation test still passes SCM_RIGHTS bytes and 100 FD/VMA cycles.

Raw results: `shared-cpu-imported-pool/`. Windows runtime allocation is not implemented
and no Windows allocator qualification is claimed. No production backend uses these
new internal helpers yet. Next: explicitly selected extension handshake/channel
routing and live frame ownership, then coordinated GStreamer/A/V/rate validation.
