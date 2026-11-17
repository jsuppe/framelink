# Windows shared-CPU allocation prerequisite

Internal `SharedCpuAllocationWin` implements unnamed committed pagefile sections,
not a production channel, public factory, or negotiated transport capability.
Windows allocationBytes explicitly includes system-page padding; reject unaligned
sizes without rewriting descriptors. FCS1 remains unchanged and permits padding.

Import independently checks exact section size and SEC_COMMIT-only attributes via
NtQuerySection. File-backed, image, reserved, uncached, and unknown attribute
combinations reject. Missing query support fails unsupported, never falls back.
Query+read[+write] exports are non-inheritable and scoped to a supplied process;
authenticating that process and transferring ownership are future transport duties.
MapViewOfFile tests actual rights. Read-only imports cannot upgrade via this API.
Mapping references retain handle/views beyond wrapper close; final release unmaps.
Epoch/allocation-ID are checked against separately supplied expected values.

## Evidence

Windows 6/6 tests pass: allocator, pool preflight, descriptor C++/C, session and
memory contracts. Allocator checks padded BGRA/NV12 every active pixel, 100 local
allocate/map/release cycles, 20 child-process imports (after one process-runtime
warmup), exact handle return after each pair, VirtualProtect write-upgrade denial,
retained-view visibility, final unmapping, stale ID/epoch/size/type/query-right
negatives and unchanged outputs on failure. These are small functional fixtures,
not target-rate or general D3D resource-endurance qualification.
Linux CMake reconfiguration and three shared descriptor/pool regressions pass.
Windows test source hashes and logs: [windows-shared-allocation](windows-shared-allocation/).
Remote tree is staged, not a git checkout; source hashes, not remote HEAD, identify it.

Initial test compilation shadowed C exit() and was fixed. Initial child-process
run failed a cold handle baseline; one CreateProcess warmup plus 20 process checks
now passes the unchanged exact-return criterion. Original negative logs retained.
Loom pre-edit command unavailable (`loom: command not found`); no delegation used.

## Next

Atomic Windows imported-pool identity checks, retired-lease isolation and bounded
history; then authenticated handle transfer, ready/release and coordinated CPU
sessions/bins. Windows D3D11 producer integration, lifecycle growth, timing/fault
acceptance and reviewed API integration baseline remain open. No API freeze/M6
closure, no M7 advancement, no deferred Linux/Android campaign restart.
