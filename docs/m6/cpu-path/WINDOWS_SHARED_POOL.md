# Windows atomic shared-CPU pool prerequisite

Builds on f642162 allocation storage; no public API or wire-format change.
`SharedCpuImportedPoolWin` connects existing coordinator policy/geometry/epoch
preflight to actual Windows section imports. All slots stage before replacement.
Consumers retain reduced read-only handles; producers must prove writable maps.

`CompareObjectHandles` compares retained kernel objects, not handle numbers.
Unavailable API rejects with unsupported. Weak identity tokens track at most256
live allocations across current/retired generations. Held old mappings prohibit
backing reuse under new IDs/epochs; released generations do not stay pinned by
the registry. Full history returns BUSY and recovers when mappings are released.

Windows7/7 focused tests pass, including existing allocation and portable C/C++
contracts. Pool test:100 failed partial imports return exact handle baseline,
alias rejection, stale/foreign session and epoch negatives, cross-generation
held pixel visibility, consumer write restriction, producer read-only rejection,
move ownership,128 generations with256 pinned maps, BUSY and release recovery.
No change to Linux/Android production code. No rate/endurance qualification claim.

Raw evidence: <linux-home>/dev/evidence/m6-cpu-path/windows-shared-pool.
Archived source hashes/logs: [windows-shared-pool](windows-shared-pool/).
Next: authenticated Windows CPU control endpoint/handle transfer and explicit
pool acknowledgment; preserve existing ready/release and metadata/EOS contracts.
Do not advertise a Windows CPU channel before live ownership protocol validation.
D3D11 coordinated producers, resource growth, timing/fault gates and API baseline
remain incomplete; M6 open, hold before M7.
