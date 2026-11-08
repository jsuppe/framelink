# Stable shared allocation and retained mapping ownership

`src/fl_shared_cpu_allocation.h` is an internal Linux/Android building block.
It does not yet implement a FrameLink channel, native image type, coordinator
profile, Windows allocator, or frame-lease protocol. Existing defaults and native
resource APIs are unchanged.

- Creation uses a memfd with grow, shrink and seal-list seals. Import duplicates
  the caller's FD with CLOEXEC, verifies regular-file type, exact allocation size,
  fixed size/seal list, descriptor validity and separately supplied expected
  generation/allocation identity. Caller-owned input handles are not consumed.
- Descriptor identity is a control-layer assertion, not an OS-handle identity or
  cryptographic guarantee. Authentication and handle-to-pool binding remain
  integration prerequisites.
- Read-only exports reopen an O_RDONLY file description; they are not writable
  duplicates bearing a software-only label. Writable requests against read-only
  or write-sealed resources reject. No rights upgrade or silent fallback occurs.
- Mapping views retain shared ownership of the FD and VMAs after allocation
  wrappers and exported FDs are destroyed. At most one read-only and one writable
  VMA are cached per state. The final mapping reference releases them. Mapping
  references do not authorize concurrent producer overwrites: the negotiated
  frame ownership/ACK protocol must govern pixel access.
- Read/write mappings are synchronized for creation; allocation-wrapper reset
  and mutation require external serialization. A retained Mapping may outlive
  that wrapper. No GPU usage/capability is advertised by this allocator.
- `/proc/self/fd` reopening is required for rights downgrades. Unavailable access
  fails rather than returning a writable handle to a read-only caller.

## Validation and resolved failure

Linux passes allocation + descriptor/C11 tests (3/3). Android arm64 allocation
execution passes; arm64 and x86_64 builds pass. Tests cover mutable/unsealed and
wrong-size/type imports, stale generation/identity, output preservation, denied
write/export upgrades, real OS read-only mapping rejection, retained references,
BGRA/NV12 descriptor+SCM_RIGHTS transfer between separate processes with full
byte checks, and 100 create/map/release cycles returning both FD and named VMA
counts to their initial values. This is not the full M6 endurance resource gate.

The initial Android run rejected a read-only mapping of a write-sealed file
through an O_RDWR description (read result ACCESS_DENIED, seals0xf). Import now
reopens write-sealed inputs genuinely O_RDONLY. The same test then passes without
weakening write denial, file seals, or pixel checks. Original failure and diagnostic
logs are retained in `shared-cpu-allocation/` alongside final results/hashes.

Next: explicitly negotiated channel/coordinator integration must preserve pool
and frame lifetime, reject native GPU interop on CPU-only resources, and test
reconnect, retained frames, malformed descriptors and copy permissions. Windows
allocation remains separate work; Windows portable descriptor tests alone do not
establish a Windows shared-CPU path. M6.8b/c remain incomplete.
