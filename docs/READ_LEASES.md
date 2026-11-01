# Downstream read leases

Create the consumer with `FL_DEFER_RELEASE` (not `FL_GPU_SYNC`). Acquire a frame
normally, then transfer that held frame with `flLeaseFrame`. The token owns one
reader; `flRetainReadLease` creates a distinct reader token. `flReadLeaseFrame`
returns canonical frame information and allocation generation while the token is
live. Retain before handing a token to a worker; serialize all channel operations.

Finish a reader in one of three ways:

- After actual CPU/GPU access has finished, `flCompleteReadLease`.
- Linux/Android: `flCompleteReadLeaseSyncFd` duplicates the completion FD.
- Windows: `flCompleteReadLeaseD3D11Fence` imports the shared fence and target value.

Successful completion or fence handoff consumes that reader token. Do not access
its image or reuse its token afterward. Call `flCollectReadLeases` regularly to poll
completion and publish retirement. If collection fails, retry collection, not
completion. A later completed frame cannot retire an earlier outstanding reader.

Bounded capacity: at most 16 acquired frames and 256 readers per channel. These are
tracking limits, not an allocated image count or latency target. Existing configured
pool size is unchanged. Exhaustion returns `FL_BUSY`; no silent growth or overwrite.
Token values never recycle during the loaded library's lifetime.

Do not mix managed leases with manually signalled consumed fences. Complete and
collect **all** readers before closing the channel. `flClose` is not a GPU drain.
Fence/device errors leave ownership retained; recover only after establishing GPU
quiescence. Pending readers prevent resize and producer reconnect. New allocation
or connection epochs reject old tokens. No wire-format change is required.

Current tests qualify native ring ownership and deterministic completion ordering,
not cross-process decoder allocation negotiation, relay, throughput or presentation.
Separate-process RGBA8 GPU reads and real completion-fence export now pass on
Linux/Windows/Android; see `m5a/read-lease-gpu-20261101/`. Delayed/out-of-order
POSIX controls still use eventfd; they are distinct from the driver test.
