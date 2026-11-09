# M6 low-latency and native-memory resolution plan

Revision 1 — 2026-11-04. Owner: project executor, one task at a time.
Status: approved user requirements plus implementation proposals; no new runtime
qualification is claimed by this document. Applies to FrameLink and vkrender.

## Confirmed requirements and evidence boundaries

Jon confirmed on November 4 that the primary use case is zero-added-latency
process boundaries, not 150 ms buffering. Preserve original A/V timing across
hops and use native allocation sharing where compatible. Pool capacity is not a
playout delay. Mandatory ownership and readiness synchronization must never be
removed to obtain a lower latency number. Literal zero elapsed time is not a claim.

M1–M5b remain complete within their published scope. M6.1's existing matrix and
150 ms measurements remain historical evidence, not acceptance of the primary
low-latency mode. M6.2 is incomplete; a722b75 repairs receive queue handling but
post-fix full qualification is not established here. M6.8/M6.9 are open gaps.
Keep existing order M6.2–M6.9, then M7. M6.7 is automation infrastructure, not
permission to close M6 before M6.8 and M6.9 and their final integrated reruns.

## Decision register

For every proposal below record: accepted/rejected/superseded, decision owner,
date, rationale, public API/default impact, implementation revision and evidence.
Do not silently promote a recommendation to a user decision. Jon owns product
tradeoffs; executor owns routine implementation choices within accepted scope.

### D1 — Timing ownership and defaults

**Confirmed:** no intentional per-hop buffering in the primary mode.
**Proposed:** transport-only boundaries immediately expose ready buffers and
preserve the common media timeline; only the final playback stage schedules
presentation. Set the primary session/example profile to zero added transport
latency; retain explicitly selected buffered playback for compatibility.
Inventory all BaseSink sync flags, bin latency, app queues and renderer waits.
Do not set every final audio/video sink to sync=false: that destroys playback
synchronization. Do not independently rebase audio and video at each hop.

Alternative: retain 150 ms default and require opt-out. Not recommended because
it makes the primary use case opt-in and can conceal throughput failures.
Implementation decision (executor, November6): apply the confirmed zero-added
transport requirement to the public `framelinkavsrc` default (0ms). Retain the
existing explicit `latency-ms=150` setting for compatibility instead of adding a
redundant profile API. Migration notice and scheduled-playback regressions are
required; no claim of a silently compatible default change. Application-managed
clock setup and explicit final-playback samples remain unchanged.

### D2 — Queue capacity, overload and loss semantics

**Proposed:** default to the smallest validated ownership pool, test two buffers
first, and permit additional allocations only with a measured ownership reason.
No fill-before-delivery or queue-depth-derived delay. Keep pool size, unread
queue bound and scheduling latency independent. Publish actual effective values;
strict negotiation rejects unsupported depth rather than silently clamping it.

A finite-memory transport cannot guarantee zero waiting, zero drops and lossless
operation under an arbitrarily stalled consumer. Propose explicit policies:
- realtime/latest: bounded stale-video replacement, reported identities and age;
- lossless: bounded backpressure, with latency allowed to rise and reported;
- qualification/strict: fail on unexpected loss or unsupported allocation policy.
Never overwrite a GPU-owned frame. Audio must not silently use latest-frame-wins;
retain sample continuity or explicitly signal discontinuity/failure.
Decision needed: realtime overload behavior and API default. Preserve existing
entrypoint behavior until resolved; do not change it just to pass. API audit
2026-11-09 found the prior “current fail-fast” wording overbroad: thin native
copy-in defaults error-on-ring-full=false (drop-and-count); strict profiles opt
into failure. No uniform default or product decision is inferred. Exact observed
defaults and remaining property gaps: windows-api-candidate/README.md.

### D3 — Native producer integration and fallback

**Proposed:** bind M5a allocation/usage negotiation and completion fences to
coordinated sessions (M6.9), both individual elements and A/V bins. Support
upstream allocation into a negotiated pool where compatible, and external
allocation import where supported. Preserve identity, format/planes, stride,
modifier, adapter, generation and lifetime through every boundary.

Strict zero-copy rejects incompatibility before media. Optional copy fallback
must be explicitly selected and report reason, bytes and direction (CPU copy,
upload, readback, conversion). GPU-to-GPU copies are copies too. Generic decoder
compatibility is not established by a synthetic producer alone.
Decision needed: whether convenience callers default to strict or explicit
fallback profile. Recommendation: strict in the native profile; separately named
CPU/compatibility profile, never a hidden fallback.

### D4 — CPU versus GPU memory placement

**Proposed:** negotiate intended consumer access rather than assuming all native
memory is CPU-efficient. For CPU-heavy consumers evaluate cached shared system
memory versus explicit staging/readback; for render/decoder consumers preserve
native GPU allocations. Unified-memory platforms still need cache/fence checks.
Mixed CPU/GPU consumers must report the chosen compromise and actual transfer
costs. Do not promise simultaneous optimal placement or universal zero-copy.

Linux M6.8 compares cached memory, streaming access and staging on the same
workload. Android M6.5 isolates NV12 conversion, lock/cache and stride costs;
unsupported exact layouts must reject. Windows qualifies readback, adapter
compatibility and queue behavior rather than assuming Linux findings apply.
Selection depends on measured support; no vendor-specific solution is mandated.

### D5 — Qualification budgets

**Confirmed:** 1080p60 and 4K30 remain target workloads, not tiny-frame substitutes.
**Proposed gates:** zero configured transport hold; no unexplained nominal loss;
no hidden copies in strict native mode; bounded memory/leases; exact propagated
A/V timestamp offset. Test one and two boundaries with matched source work.

Before each campaign freeze numeric per-platform p50/p95/p99/max boundary-age,
CPU/GPU and overhead budgets. Proposed initial added-hop p95 target: <=2 ms;
this is a proposal, not a measured universal guarantee (older evidence includes
~2.1 ms). Report misses, do not tune the budget after observing a failing run.
Physical scanout/acoustic alignment are separate from API completion and PTS.

## Executable work breakdown

All unchecked tasks below are pending, not newly completed by prior large-queue
runs. Every validation records revision, host/driver, settings, exact command,
trace state, all failures, and raw evidence. Execute sequentially.

### M6.2 — Acceptance suspended; evidence published

2a source inspection completed: [timing audit](TIMING_AUDIT.md). 2b partial: independent timing, bounded output, paired admission/residence probes,
all-frame timing and explicit preroll controls are validated in
[platform controls](zero-delay-platform-controls/README.md). Primary-profile
budget acceptance remains open; these measurements do not prove physical output.
- [x] 2a Audit timing/queue behavior in bins, elements, renderer and benchmark;
      publish where each wait/copy occurs and reconcile D1/D2 with current code.
- [ ] 2b Parameterize latency, pool and unread depth independently; add primary
      zero-added-delay and explicit buffered-control profiles; expose effective
      settings. Reopen M6.1 budget acceptance for the primary profile.
- [x] 2c Review a722b75 ownership, fence disposal, queue shrink, disconnect,
      monotonic release and actual depth; test unsupported-depth rejection.
- [x] 2d Add deterministic fast-consumer, delayed-consumer and burst controls:
      distinguish producer gaps, overwritten pending frames, GPU-held slots,
      clock scheduling and upstream loss. Verify no wait-to-fill.
- [ ] 2e Run affected full suites on Linux/Windows/Android; then 30-minute
      supported 1080p60/4K30 cases with zero-added-delay/small pools. Preserve
      startup, steady-state and drain accounting. Record other host load.
- [x] 2f Publish all pass/fail cells and queue-age distributions. Large-pool or
      150 ms control success cannot waive the low-latency gate.

Deterministic2d evidence: [queue/clock controls](queue-controls/README.md), commit0ff18eb; Linux/Windows/Android passed. Held-reader controls emulate ownership delay, not physical GPU latency.

Receive-depth gate evidence: [review and platform tests](receive-depth/README.md).

### M6.3 — Current execution: lifecycle endurance
- [ ] 3a At least 100 restart/reconnect cycles per target in the primary profile.
- [ ] 3b Include outstanding native leases, generation changes, EOS and pause;
      ensure cancellation is bounded and resources return to warm baseline.

### M6.4 — Fault and overload qualification
- [ ] 4a Resolve D2 before changing public overload defaults.
- [ ] 4b Inject consumer/GPU stalls, producer bursts, endpoint death, audio
      underruns and malformed/stale metadata. Validate each selected policy.
- [ ] 4c Verify bounded recovery, explicit loss/discontinuity, and no unsafe reuse.

### M6.5 — Performance and platform cost analysis
- [x] 5a Measure software call-boundary copy/lock/wait/convert/present costs and resource growth. Evidence/scope: cost/STAGE_COSTS.md. GPU execution, physical scanout and primary-renderer qualification are not inferred; remaining gates stay open.
- [x] 5b Isolate Android NV12 CPU costs with format/layout and lock controls. Evidence: cost/android-map, android-nv21-fix, android-cpu-controls and android-cpu-trace; target CPU throughput remains unqualified and carries into M6.8.
- [ ] 5c Separate Windows queue timing from producer and GPU scheduling defects.
- [x] 5d Publish platform/format/resolution limits; failed and unqualified cells explicit in PLATFORM_LIMITS.md.

### M6.6 — Observability without hidden overhead
- [ ] 6a Join independent source identities through each boundary, GPU completion
      and presentation submission; separately track PCM and A/V offset.
- [x] 6b Compare trace off/on, quantify memory per recorder and loss detection; evidence observability/TRACE_OVERHEAD.md.
      Default reduced to4096events/thread; explicit sizing and overflow reporting validated on all platforms.

### M6.7 — Repeatable automation (closure infrastructure)
- [x] 7a Add deterministic queue/ownership/timing negatives to CI. Hosted CPU trace checks and opt-in native hardware selection updated; local Linux19pass1skip/Windows20pass evidence in automation. HostedActions execution is not claimed; final integrated CI remains7c.
- [x] 7b Publish reproducible real-device scripts and machine-readable gates. tools/m6_validation.py validated on Linux/Android/Windows with explicit skipped/missing/failed accounting; automation/RUNNING_GATES.md.
- [ ] 7c Ensure final acceptance references the exact integrated revision after
      M6.8/M6.9; old benchmark passes are not final qualification.

### M6.8 — CPU-consumer memory path repair
- [x] 8a Reproduce Linux slow/bimodal readback with allocation/cache diagnostics. Evidence: [CPU-path investigation](cpu-path/README.md); no production repair/target-rate claim.
- [ ] 8b Compare cached shared-memory, streaming and explicit staging choices;
      implement negotiated CPU-efficient path without mislabeling copies.
- [ ] 8c Validate pixels, planes, visibility, lifetime and A/V timing; measure
      target-rate CPU consumption on all three platforms where supported.

### M6.9 — Zero-copy native producer in coordinated sessions
- [ ] 9a Connect native negotiation to session coordinator, sink and A/V bin.
- [ ] 9b Integrate upstream allocation/import and readiness/completion fences for
      DMA-BUF, D3D11 and supported AHardwareBuffer layouts.
- [ ] 9c Reject incompatible adapter/layout/usage and test explicit fallback.
- [ ] 9d Qualify real compatible decoder/renderer producers, native consumer,
      and two-hop relay with PCM; prove allocation identity and zero payload
      copies, not just absence of CPU mapping.
- [ ] 9e Repeat affected endurance/performance/trace gates on the final integrated
      revision, review all unresolved decisions and verify CI/merge before M6 closes.

### M7 — Release and developer handoff
- [ ] 7.1 Freeze API/ABI, decision records, capability matrix and timing migration.
- [ ] 7.2 Ship zero-added-delay Windows/Linux/Android examples, native decoder
      integration recipes and an explicitly buffered playback example.
- [ ] 7.3 Validate clean installation and observable effective settings/copy paths.
- [ ] 7.4 Review exact-head tests, merge/tag/artifacts and known limits; retain
      physical-output exclusions unless independently measured.

## Immediate next action and coordination

## Priority override — Jon, 2026-11-08 16:29 EST
Jon requests completing all remaining Windows-specific changes first and stabilizing the API. This supersedes the previous Linux/Android refresh order. Preserve and collect the currently running Linux BGRA1080p60 test (already near completion), then do NOT launch the queued Linux NV12 refresh. Defer additional Linux/Android campaigns except focused checks required by shared API/code changes.

Next, inventory and execute remaining Windows M6 implementation and repairs sequentially: shared-CPU allocator/transport integration; Windows D3D11 native-producer integration into coordinated sessions (M6.9); reconnect/device lifetime memory growth; outstanding Windows timing/queue defects and required fault handling. Validate each change with focused Windows checks. Track genuine blocked items with owner/evidence/resume condition, not silent acceptance.

Make API stabilization an explicit deliverable alongside this Windows-first work: audit native C/platform APIs and GStreamer properties/session contracts, resolve overload/fallback defaults, preserve compatibility or document versioned migration, and check planned Linux/Android integration will not require breaking the proposed contract. Publish an exact revision and compatibility checks for an app-integration baseline. Do not claim the API is frozen until reviewed/tested; do not equate a Windows-ready API baseline with full cross-platform M6 completion. Bring forward API stabilization work, not unrelated M7 packaging/release; retain maintenance hold before M7.


Sole executor Evelyn, sequential only. M6 is not complete.

Single 1800-second diagnostic on Android, base93283ac / diagnostic0f1e89a4. Independently verified 108000 video / 86400000 PCM frames at each endpoint, 180000 contiguous audio blocks, and complete unique 0..107999 sink/consumer stage coverage. Exit/pull/analysis0, zero output fallbacks. All startup and drain frames included.

No 17ms miss reproduced: arrival p95 8.499278ms, maximum12.928569ms; audio maximum6.354042ms, common inferred A/V mapping delta0ns. Consumer copy wall p95 3837us, CPU p952887us; wall-minus-CPU p952819us. Separate quantiles are not additive. Original failure phase index78795 stages retained. Resource slopes consumer6.41KB/min; see raw summary for producer/resources.

**Unresolved, not qualified.** The prior trace-off17.863579ms failure remains an acceptance failure. Trace instrumentation changes scheduling; this successful diagnostic does not waive it or identify a remedy. No blind repeat. NV12 1080p60 and BGRA4K30 acceptance suspended, owner Evelyn, resume after an evidenced mechanism plus matched control. No production changes.

Raw durable evidence: <linux-home>/dev/evidence/m6-cpu-path/android-nv12-residual-audit. manifest.sha256 records all raw inputs and scripts. validate.py reproduces analysis from that directory.

**Current sole execution (2026-11-09 UTC):** Full Windows b997e7c build and serial suite collected: build exit0,124/124 tests passed (567.23s); evidence [integrated regression](windows-integrated-b997e7c/README.md). No duplicate or Linux/Android campaign. Broker-bound Windows CPU pool exchange/ACK now passes5/5 focused tests,12 real child-process BGRA/NV12 pools and57 negative cases with exact handle cleanup; [scope/evidence](cpu-path/windows-cpu-control/README.md). Windows ready/release metadata/EOS now validated5/5:100 child sessions,3200 exact frames,80 negative cases andemptyEOS; [evidence](cpu-path/windows-cpu-frames/README.md). Next: public Windows CPU channel/delegation andauthenticated coordinator binding. Auth/discovery and public channel are not implied by internal setup. Preserve M6 gates and M7 hold.

Recovery 2026-11-09 UTC: Linux BGRA1080p60 input-retention completed at99a8318; collected without rerun, unchanged four binary hashes verified. Independently validated108000video/180000contiguous audio rows, summary86400000PCM, zero17ms video/audio misses, p955.578372/max12.449484ms, inferredAVmapping0ns, RSS3.23KB/min both. Archived cpu-path/shared-cpu-benchmark/linux-bgra-1080p60-input-retention-sustained. At initial receipt no local/Android/Windows executor was observed. EXECUTION_GAP corrected by beginning Windows shared-CPU allocator implementation in this recovery session. Windows allocator prerequisite now validated: exact committed-pagefile size/type, OS read-only rights, retained maps, 100 local cycles and 20 child imports; Windows6/6 and Linux3/3 shared-contract regressions. See cpu-path/WINDOWS_SHARED_ALLOCATION.md. Windows atomic imported pool now validated7/7: actual kernel-object alias checks, read-only consumer rights, atomic imports, retained-generation isolation, bounded256 weak identities and BUSY/release recovery. See cpu-path/WINDOWS_SHARED_POOL.md. Windows control framing prerequisite repaired: undeclared trailing bytes now reject; negative reproduced before fix and21 affected Windows tests pass (windows-ipc-framing). Bounded Windows CPU pipe prerequisite now validated4/4: async-only adopted local message pipe, stable peer-process binding, exact framing, read/write timeout, cancellation drain and100 handle-clean exchanges. See cpu-path/windows-cpu-pipe. Broker authentication and cross-process handle transfer remain next; no public CPU channel yet. Next: authenticated Windows pool handle exchange/acknowledgment, then ready/release/coordinator integration. Legacy pipe I/O remains synchronous and unchanged. Additional Linux/Android campaigns deferred except shared-change regressions. Exact revision609cb74 now builds clean in an isolated Windows tree and passes31/31 selected tests. Runtime properties/DLL hashes and source-header manifest are published in windows-api-candidate/README.md. API inventory found entrypoint-specific overload defaults; preserved, D2/D3 unresolved. Candidate only: CPU Windows channel and coordinated native producers still pending; no freeze claim. Existing acceptance failures remain unresolved; M6 open and hold before M7.


- M6.2a/c/d/f validated; current supported matrix14 captures, independent counts and frozen diagnostics in PRIMARY_RESULTS.md. M6.2b/e acceptance suspended: original159ms Windows event and isolated-hop/product budget evidence remain unresolved. No clean-repeat waiver.
- M6.3a Linux and fixedAndroid100/100 restartcycles in both directions passed resource gates. AndroidEOSrace repairedf8f2483, allplatform affectedtests andAndroidendurance passed. Originalcrash preserved.
- Windows3a memory slopes21.10/84.09KB/cycle fail16. RawD3Ddevicechurn reproduces growth, exclusive retaineddevice flat, Trimnegative. No implicitcache/APIchange. Resource acceptance suspended per lifecycle/WINDOWS_DEVICE_LIFETIME.md with owner/resumeconditions; no blanket attribution orwaiver.
- M6.3b functionallease/generation/EOS/pause/boundedcancellation checks passallplatforms (lifecycle/ownership andeos-fix). Sameprocess100heldlease/reconnectcycles allfunctionalpass; LinuxRSS0.142/Android-0.509KBcycle pass, Windowsprivate82.528FAILS16, handles/threadsflatall. Evidence lifecycle/lease-cycles. Windowsmemory portion remains suspended under same dependency;M6.3unchecked.
- M6.4 existingpolicy8testsperplatform pass, plusWindowsD3D11normal/death/controlledGPUwait3/3. Faultscope in fault-coverage. Linux/AndroidEGLcontrollablewait injection unavailable77, notpass; rawnegativepreserved. M6.4b/c GPU-delay subset SUSPENDED pending supportednativefenceinjection; D2publicdefaultchoice unresolved, nochange. Existingpolicies testedexplicitly, no newlyapproveddefault inferred.
- M6.5a/b/d,6b,7a/b validated within documented scopes. Gate runner24bcd78: Linux/Android19pass1skip, Windows20pass; final7c waits8/9.
- M6.8a complete: allocation/map/streaming/cached and GL-staging diagnostics archived in cpu-path/README.md. Staging no-rebind negative retains stale pixels; existing native-fence benchmark does not reproduce it. Full-frame visibility regression e7bde0f passes Linux/Android with corruption detection.
- M6.8b partial: 6275fe5 implements explicitly selected Linux BGRA/RGBA gpu-readback with adopted allocation and both-endpoint COPY|READBACK permission. Default/native paths unchanged. Linux9affected+2finaladapter, Windows9affected, Android6affected andbothABIbuilds passed. Each trace records32 GPUcopy and32readback events,663552000bytes each. Initialcoldmax115.609ms andfinalcoldmax77.604ms retained; no budget/ratepass inferred. Evidence: cpu-path/EGL_READBACK.md.
- e6a7acf adds real cross-process cached shared-CPU comparison:32/32exact BGRA/NV12 frames at1080p/4K. This is an allocation control, not a FrameLink shared-CPU path. No executor remains running; all commands/results collected. SharedCPUv1 descriptor nowimplemented: independenttagged128bytecodec, bounded/nonoverlappingplanes, unchangedlegacyprotocols; C11+1024mutation+policynegatives andexistingcontracttests7/7each Linux/Windows/Android pass; bothAndroidABIbuilds. Internal Linux/Android stable-size allocator/import nowvalidated: trueROexports, grow/shrink/seal-listchecks, retainedmappingownership, SCM_RIGHTS BGRA/NV12pixels and100cleanFD/VMAcycles. InitialAndroid write-sealedO_RDWRreadonlymapfailure fixedbytrueROreopen; originalnegative retained. Linux3/3, Androidruntime+dualABIbuildpass. Internal coordinator pool preflight nowvalidated: sessiongeneration/geometry, uniqueIDs, aggregate1GiB/2..16slots, queuedepth andboth-endpoint COPY/CPU-only policy. Linux6/6, Windows5/5, Androidruntime+dualABIbuildpass; cpu-path/SHARED_CPU_POOL.md. This is not a live channel/negotiation capability. AtomicLinux/Androidimportedpool nowjoins preflight+actualFDidentity/rights: duplicatebackingobjects rejected, all-or-nothingadoption, strictsessionreplacement, retainedoldmappingprotection withbounded256weakidentityregistry andBUSYrecovery. Linux5/5, Android2runtime+dualABIbuildpass; cpu-path/SHARED_CPU_IMPORTED_POOL.md. No productionbackend advertises it yet. ExplicitinternalCPUcontrol nowexchanges FCH1 exactsession/role/bothendpointpolicy andFCP1 descriptor+SCM_RIGHTS pool, failclosed nolegacyfallback; realforkedpixelidentity and80malformedFDcleanupcases pass. Linux7/7 Android2runtime+dualABIbuildpass; cpu-path/SHARED_CPU_CONTROL_SETUP.md. No publicfactory selects it. ExactFCA1 poolacceptance nowrequired beforeacceptedstate; malformed/stale/FD/silentpeer negatives pass, Linux7/7 andAndroidruntime+dualABIbuild. ACKsendfailure leaves locallyimportedpool owned butconnectionfailed; no mediaallowed. InternalFCF1/FCR1 ready/release nowchecks exactgeneration/slot/sequence andCPUcompletion contract;32forkedfullpixel/PTSframes and11hostilereadycases pass, Linux7/7 Androidruntime+dualABI. CPUrawpointerdiscipline stillcallerowned; notpublicleases. Releasequalification nowpasses16slotsaturation/reverseorder and15hostile/replay/death/silentheldcases, Linux8/8 Androidruntime+dualABI; no productionfixneeded. PublicPOSIX CPU-only opaquechannel/lease API nowimplements explicitconnectedfactory, reservedwrites/cancel, retainedreadfinalrelease andclose-heldmappinglifetime; nativeGPUtypes separate/noexports. Linux12/12 Androidpublic+C11runtime+dualABIpass; cpu-path/SHARED_CPU_CHANNEL.md. Publicreconnect100freshgenerations eachLinux/Android nowpassesheldoldpixels/newpoolisolation andexactFD/VMAcleanup; dualABIbuilds, cpu-path/SHARED_CPU_RECONNECT.md. Caller/coordinatorstillmustvalidateepochfreshness. Strictopt-inmetadata factoryEx mode nowpreservesDTS/duration/discontinuity/mediageneration, rejectsmismatchedpeers/malformedmetadata withoutdowngrade; Linux13/13 Android3runtime+dualABIpass, cpu-path/SHARED_CPU_METADATA.md. OrderedCPU EOS nowvalidated: strictboth-peeropt-in FCE1generation/finalsequence, retainedreadsdrainafterterminalmarker, emptystream/disconnectdistinction/sevenhostilenegatives, Linux16/16 Android6runtime+dualABIpass; cpu-path/SHARED_CPU_EOS.md. Explicitcoordinator CPU FDdelegation nowvalidated: exactgeneration/role singleclaim, cancelsblockedsetup/claimedI/O withoutfreeingleases, ownerclose shutsdownduplicates. Linux19/19 Android8runtime+dualABIpass; cpu-path/SHARED_CPU_COORDINATOR.md. Explicitframelinksrc shared-cpu copyout nowvalidated forbroker-boundvideo-only sessions: actualCOPYchecked, paddedplanes/exactpixels/metadata/sharedclockmapping, EOSvsdeath andboundedcancel. Linux25/25 Android13runtime+dualABIpass; finalsourcefocusedtests/trace17copies13824bytespass. cpu-path/SHARED_CPU_GST_SOURCE.md. ExplicitCPU sinkcopy-in nowvalidated withactualCOPY/SystemMemoryonly, boundedqueueownership, terminalleasedrain andcancellation. RealvideoCPU sink/source BGRA/NV12 roundtrip passes; Linux26/26 Android14runtime+dualABI,100roundtriprepeats; finalaccountingfocusedLinux/Androidpass. cpu-path/SHARED_CPU_GST_SINK.md preservesinitialunlabelledtestfailure/fixturecorrection. CPUauthenticatedevents nowjoinexactafterFrames/PTS GAP/barrier/END; no skippedbarriers, mappedGAPs, boundedFLUSHcancellation via nonconsumingreadinesswait. Linux27/27 Android15runtime+dualABIpass; finaldeadlineclamp3focusedtests/passbothplatforms, cpu-path/SHARED_CPU_GST_EVENTS.md. A/Vterminalcoordinatorprerequisite nowtested: empty/nonempty idempotentobservations, contradictorycounts/PTS, replacementreset/stalewait andboundedcancellation; Linux3/3 Android3runtime+dualABIpass, cpu-path/AV_TERMINAL_PREREQUISITE.md. IntegratedCPUvideo+authenticatedPCM nowpasses exactBGRA/NV12pixels/PCMbytes, commonmapped37msoffset, bothEOSsubmissionorders/emptyaggregateEOS, terminalconflict/PCMdeath andboundedteardown. Linux5/5 Android5runtime+dualABIpass; cpu-path/SHARED_CPU_AV.md. A/Vrequiresauthenticatedeventstream; brokerprovidedFDsonly. Explicitshared-cpu binselection nowvalidated: same6integratedcases throughbothbins plus5configurationnegatives; Linux6/6 Android6runtime+dualABIpass includingexistingnative/localbinregression, cpu-path/SHARED_CPU_BINS.md. CPU session=discovery stillrejects, nofallback. CPUbootstrapSTREAMauth prerequisite nowimplemented: separatecredentialdomain/fullconfig/freshVrole, wrongkey/epoch/generation/clock/crossdomain/deadline/cancel negatives; Linux3/3 Android2runtime+dualABIpass, cpu-path/CPU_BOOTSTRAP_AUTH.md. AuthenticatedUnixSTREAM endpointtransfer nowimplemented: freshSEQPACKETpair/exactsingleSCM_RIGHTS/CLOEXEC/ACK/wholedeadline/epochchecks; wrongtype/missing/multiple/truncatedFDs/marker/silentpeer rejectwithexactFDcleanup. All12thin/binA/Vcasesnowusetransferredendpoints. Linux3/3 Android2runtime+dualABIpass, cpu-path/CPU_ENDPOINT.md. ProtectedtypedUnixdiscovery74f6428 nowvalidated: independentpublicsocketname/privatecredential,FAR2notFAR1,ownerexclusion/crashrecovery; Linux4/4 Windows3/3 Android3runtime+dualABI, twoAndroidfilesystemfixtureexclusions documented. LocalCPUsetup2df5ce4 nowcomposesdiscovery/auth/clock/PCM/events/endpoint/pool;16A/Vcasesincluding4localsetup,5negativecases, Linux5/5 Windows3/3 Android4runtime+dualABIpass. DirectCPUbin session= nowvalidated:20integratedA/Vcases plusbounded60s-discoverycancellation; Linux5/5 Windows4/4 Android4runtime+dualABIpass. IndependentLinuxprocessBGRA/NV1264x64smokes deliver32exactframes+25920stereoPCMframes each, source/receiverSHA256andlengthsmatch; nofullresolution/rateclaim. Evidence LOCAL_CPU_BINS.md. Allcommandscollected. Explicitshared-CPUbenchmarkselection nowvalidated: Linux/Android BGRA/NV1264x64 each150frames/240000PCM, exactly150copyevents perendpoint andexpectedbytes; Linux3/3 regressions,Windows2/2,dualAndroidABIbuilds,5invalidoptionnegatives. Evidence cpu-path/SHARED_CPU_BENCHMARK.md. 1080p60BGRA preflight300/300video+240000PCM passed,zero17msmisses,commonAVmapping0ns. First sustained sharedCPU cell validated: Linux BGRA1080p60 1800s revision65a1268,108000video/86400000PCM,180000contiguousaudio,zero17msmisses,arrivalp956.683/max11.909ms,AVmapping0ns,consumer/producerRSS3.23KBmin each<64,CPU24.08/33.61%. Archived cpu-path/shared-cpu-benchmark/linux-bgra-1080p60-sustained. Three interrupted gateway attempts preserved; dynamic awaitResults has verified600000ms hard timeout, so use native fallback for1800s. NV121080p60 5s preflight passed300video/240000PCM. NV121080p60 sustained collected and validated:108000video/86400000PCM,180000contiguousaudio,zero17msmisses,p952.504/max4.297ms,AVmapping0ns,RSS3.23KBminboth,CPU9.62/13.07%; archive linux-nv12-1080p60-sustained. LinuxsharedCPU3/4 cells. BGRA4K30 sustained validated:54000video86400000PCM,180000contiguousaudio,zero34msmisses,p9524.317/max27.684ms,AVmapping0ns,RSS3.23/3.24KBmin,CPU35.32/45.96%; archive linux-bgra-4k30-sustained. NV124K30 sustained validated:54000video86400000PCM,180000contiguousaudio,zero34msmisses,p9510.382/max23.787ms,AVmapping0ns,RSS3.24KBminboth,CPU17.99/25.44%; archive linux-nv12-4k30-sustained. LinuxsharedCPU4/4 sustained cells nowvalidated, same65a1268binaries. Native handle expired but durable exit0/analysis0/summary collected; no rerun. AndroidBGRA1080p60 preflight countspass300video240000PCM but timingFAIL20/300 over17ms,p9519.511/max37.950ms,CPUconsumer97.36%; preserved android-bgra-1080p60-preflight. BGRAendurance suspended: Evelyn owns bounded copy/consumer profiling, no waiver. MatchedNV12 formatcontrol300video240000PCM passeszero17msmisses; AndroidNV121080p60 sustained validated108000video86400000PCM,zero17msmisses,p9512.032/max16.376ms,AVmapping0ns,RSS-75.10/-86.40KBmin,CPU61.19/30.76%. NV124K30preflight counts150/240000pass butfirst3framesFAIL34ms,max48.379; noendurance. BGRAtrace300exactcopies each hasduration0, notcopytiming; localBGRAcontrol300/240000passzero17msmax0.99ms. Androidmemorycontrol7200/7200pixels pass; allocation+zero/rows/contiguous medians8.856/8.945/8.816ms vs reuse2.108ms supports allocationcost, notzerofill fix. Generalized existing2buffer nonblocking outputcache andmaterializedbeforetrackready; fallback retained, paddingzero unchanged. Linux4/4, Androidsource/AV plus prior-events, dualABIbuilds,Windows2/2pass; held3outputs throughstop covercache/fallback. Evidence shared-cpu-output-cache. BGRAstill3startupmisses,max28.778ms (notqualified); NV124K30preflight150/240000passeszero34ms,max31.903. AndroidNV124K30 cache4f37f25 sustained collected:54000video86400000PCM,180000audio,zero34msmisses,p9512.382/max25.010ms,AVmapping0,RSS0.00/12.50KBmin; archive android-nv12-4k30-cache-sustained. AndroidNV121080p60 cachepreflight300video240000PCM zero17msmisses,max9.058ms. AndroidNV121080p60 cache sustained passed108000video86400000PCM,180000audio,zero17msmisses,p958.078/max12.206ms,AVmapping0,RSS-22.41KBminboth; archive android-nv12-1080p60-cache-sustained. LinuxBGRA1080p60 cachepreflight passed300/240000 zero17ms,max7.078ms. LinuxBGRA1080p60 cache sustained passed108000/86400000,180000audio,zero17msmisses,p956.762/max11.799ms,AVmapping0,RSS3.23KBminboth; archive linux-bgra-1080p60-cache-sustained. LinuxNV121080p60 cachepreflight passed300/240000 zero17ms,max2.632ms. LinuxNV121080p60 cache sustained passed108000/86400000,180000audio,zero17msmisses,p952.517/max4.598ms,AVmapping0,RSS3.23/3.24KBmin; archive linux-nv12-1080p60-cache-sustained. LinuxBGRA4K30 cachepreflight passed150/240000 zero34ms,max26.630ms. LinuxBGRA4K30 cache sustained passed54000/86400000,180000audio,zero34msmisses,p9524.548/max27.679ms,AVmapping0,RSS3.23/3.24KBmin; archive linux-bgra-4k30-cache-sustained. LinuxNV124K30 cachepreflight passed150/240000 zero34ms,max10.825ms. LinuxNV124K30 cache sustained passed54000/86400000,180000audio,zero34msmisses,p9510.595/max11.469ms,AVmapping0,RSS3.23both; archive linux-nv12-4k30-cache-sustained. CacheLinux4/4 andAndroidNV122/2 sustained nowpassed. AndroidBGRAstartup trace300/240000 has2firstframe misses,max22.252ms; sinkfirstcopy interval10.784ms, notisolatedcopy/rootcause. AndroidBGRA1080p60 diagnostic1800s collected exit0/pull0/analysis1:108000video86400000PCM,180000contiguousaudio; exactly2 startup misses(index0/1),max21.381ms,alllaterframeswithin17ms,resource slopesapproximately0. Failure retained in android-bgra-1080p60-cache-diagnostic; notqualification. Sharedmemfd first-touch control200/200bytespass: freshwrite11.058ms/2025faults versusprepared2.101ms/0faults; setupcostexplicit. Bounded555d366 sinkpreparation acquires/cancelsexistingslots andinitializesactiverows beforetrackready, withcancellation/deadline, no syntheticframes/ownershipbypass. Linux6/6 affectedtests,Androidroundtrip/AV/events anddualABIbuildpass; Windowscodeexcluded/unchanged. PostfixBGRA1080p60 preflight300/240000 zero17msmisses,max11.796ms. Evidence shared-cpu-prefault. AndroidBGRA1080p60 prefault1800s passed108000/86400000,180000contiguousaudio,zero17msmisses,max13.715ms,p959.876ms,AVmapping0,RSS-37.17both; handlesconsumer19..19 bounded18..19 (slope0.01,notnonpositive),producer17..17. Archive android-bgra-1080p60-prefault-sustained. BGRA4K30preflight150/240000 failedfirst3frames34ms,max47.207ms; later147meetbudget. Local-no-transport control150/240000 zero34ms,max5.73ms. Failurepreserved android-bgra-4k30-prefault-preflight. Evelyn owns unresolved4Ktransportstartup; noqualification/waiver. BGRA4Kdiagnostic1800s collected54000/86400000 with15misses(2startup,13at229-369s),max44.556ms; RSSnegative/audio0misses/AVmapping0. Notstartup-only,notqualification. Archive android-bgra-4k30-prefault-diagnostic. Bounded10sstageprobe showsfirstconsumer copy27.974ms vs~12ms,zero8.173ms vs~4ms; sinkfirst8.182ms vs~6.8ms. Receive/outputacquisition small; walltime notrootcause. 450sstageprobe collected13500/21600000,2startupmisses,firsttwo consumer copies507faultseach. Premap-only removesfaults but2/300misses; premap+packed-nozero diagnostic0/300. Implementedd8023c4 boundedconsumerreadpreparation andskipzeroONLYexactpackedoutputs; paddingstillzeroed. Linux17/17 Android6runtime+dualABIpass. Production4Kpreflight150/240000 retains1miss index1,pushlate16.907ms/arrival40.608ms/residual23.701ms; notqualified. Evidence shared-cpu-readprep. Source-only450s collectedexit0/EOS,CPU37.15%,handles10..10,RSSflat, but codeinspection found no counts/pushprobes: resource-only, cannotinfer13500/21600000ortiming. Corrected withtemporarydiagnostic observer sharingproducerprobe, independentbuffer/samplecounts andallPTSrows. Observed10spreflight300/480000/1000audiorows passescoverage, videomax2.97ms,audiomax71.06ms retained. Observed450s collectedexit0/EOS,actual13500video21600000PCM13500/45000timingrows. Videomax1.346389ms,no34msmisses; audio3startupmisses,max54.569ms retained. Isolatedsourcevideo doesnotreproduce transportsecondframe16.907msdelay; no source-onlyrerun. BGRA4Kacceptance SUSPENDED, Evelynowns producer-source-to-sink blocking/allocation audit; resumeidentifiedmechanism+matchedcontrol,no budgetwaiver. Diagnostic taskcomplete. Production rebuilt hashf8a42651 sourceclean d8023c4. NV124K30readprep1800s validated54000/86400000,180000contiguousaudio,zero34msvideo/audio misses,p9513.317/max18.286ms,AVmapping0ns,RSS-15.21/0KBmin,handlesbounded/threadslopes0,CPU25.54/36.05%. Archive android-nv12-4k30-readprep-sustained. NV121080p60readprep preflight300/240000 andzero17msvideo/audio missespassed. AndroidNV121080p60 readprep sustained validated108000video86400000PCM,180000contiguousaudio,zero17msvideo/audio misses,p957.624252/max11.579316ms,AVmapping0ns,RSSrounded0both,handlesbounded18..19/17 andthreads8..9/6..7. Archived android-nv12-1080p60-readprep-sustained. AndroidNV12current2/2 passes. LinuxBGRA1080p60readprep preflight300/240000 passed zero17msmisses,max6.135769ms. LinuxBGRA1080p60readprep sustained validated108000video86400000PCM,180000contiguousaudio,zero17msvideo/audio misses,p955.771877/max13.115212ms,AVmapping0ns,RSS3.23KBminboth,handlesbounded18..19/17,threads9..10/7..8. Archive linux-bgra-1080p60-readprep-sustained. Native6430expired, durableterminalresultscollected; no rerun. LinuxNV121080readprep preflight300/240000 zero17msmisses,max2.455492ms. LinuxNV121080p60readprep sustained validated108000video86400000PCM,180000contiguousaudio,zero17msvideo/audio misses,p952.191805/max5.044782ms,AVmapping0ns,RSS3.24KBminboth,handlesbounded18..19/17,threads9..10/7..8. Archive linux-nv12-1080p60-readprep-sustained; Linuxcurrent2/4 cells. LinuxBGRA4K30readprep preflight150/240000 zero34msmisses,max20.844701ms. LinuxBGRA4K30readprep sustained validated54000video86400000PCM,180000contiguousaudio,zero34msvideo/audio misses,p9519.885974/max23.029447ms,AVmapping0ns,RSS3.23/3.24KBmin. Archive linux-bgra-4k30-readprep-sustained; Linuxcurrent3/4 cells. LinuxBGRA4K30readprep validated/archive54000video86400000PCM,zero34msmisses,p9519.885974/max23.029447ms,RSS3.23/3.24KBmin; Linuxcurrent3/4. NV124K30preflight150/240000passedzero34msmisses,max9.180448ms. LinuxNV124K30readprep systemd-owned1800s completed exit0/analysis0 at d8023c4:54000video86400000PCM,180000contiguousaudio,zero34msvideo/audio misses,p958.974962/max17.719866ms,AVmapping0ns,RSS3.23KBminboth; boundedhandles18..19/17,threads9..10/7..8. Validated unchangedbinaryhashes and archived linux-nv12-4k30-readprep-sustained. Linuxcurrentreadprep4/4, AndroidNV122/2. Interrupted1750attempt preserved unqualified. AndroidBGRA4K audit identified retained internal last-sample forcing second upstream buffer: secondfill32.811ms/8100minor-faults; disablingretention9.430ms/0faults. Both10s controls300video480000PCM pass, notendurance. Implemented scoped no-last-sample for internalsharedCPUavbinvideo only; restoreonNULL/failure, thin/nativepreferencesunchanged. Newweakref regression verifiesinputreleasedbefore nextframe. Linux3/3 Android2runtime+dualABI Windows2/2 passed. Evidence shared-cpu-input-retention. ProductionBGRA4K5spreflight FAIL3/150 atindices27–29,max37.353243ms; allcounts150/240000pass, producerpushmax0.734ms, no startupmiss. No endurancequalification. Completed450s residualaudit:13500video21600000PCM45000audio,13500stage rows eachside,0fallbacks;5later34msmisses max40.153828ms. Misses dominated bysink8.842–15.946ms andconsumer18.136–28.513ms copies,CPU/offCPUmixture,notownershipstall. BGRA4Kacceptance suspendedEvelyn pendingmatchedcopy/offCPUcontrol; noqualifiedrerun. BGRA1080input-retention1800s nowvalidated108000/86400000 zero17msmisses max15.559668ms RSS-24.51both; archived android-bgra-1080p60-input-retention-sustained. NV121080input-retention1800s FAILED one17ms miss max17.863579ms; archived negative, Evelyn owns matchedcopy/offCPUdiagnosis. NV124K30input-retention1800s validated54000video86400000PCM zero34msmisses,max21.870670ms; archived android-nv12-4k30-input-retention-sustained. MatchedBGRA4K affinity and packed-plane controls complete with no demonstrated remedy; NV121080 residual diagnostic completed without reproduction; acceptance unresolved, no waiver; see immediate action. Evidence shared-cpu-residual-audit. No cleanrepeatwaiver. Changedbinrequiresaffectedfinalrate refresh. Loom unavailablechromadb. M6open; noM7. BGRAremainsopen. Evidence shared-cpu-source-observer. PriorcacheLinux4/4 andAndroidNV122/2 are pre-prefault evidence; affectedfinalgates need refresh. Nativefallback retained due verifiedgateway600s limit. Windowsallocator stillpending, thenbin/A-V/rategates. M6.8b/c andM6.9 remain open.


M6.4a default-policy decision unresolved;M6.5–9 pending. No automatedstatus reports. Notify Jon atfullM6closure andholdbeforeM7 for maintenance.
