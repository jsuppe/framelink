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
Decision needed: realtime overload behavior and API default. Retain current
fail-fast behavior until this choice is resolved; do not change it just to pass.

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

Sole executor Evelyn, sequential only. M6 is not complete.

- M6.2a/c/d/f validated; current supported matrix14 captures, independent counts and frozen diagnostics in PRIMARY_RESULTS.md. M6.2b/e acceptance suspended: original159ms Windows event and isolated-hop/product budget evidence remain unresolved. No clean-repeat waiver.
- M6.3a Linux and fixedAndroid100/100 restartcycles in both directions passed resource gates. AndroidEOSrace repairedf8f2483, allplatform affectedtests andAndroidendurance passed. Originalcrash preserved.
- Windows3a memory slopes21.10/84.09KB/cycle fail16. RawD3Ddevicechurn reproduces growth, exclusive retaineddevice flat, Trimnegative. No implicitcache/APIchange. Resource acceptance suspended per lifecycle/WINDOWS_DEVICE_LIFETIME.md with owner/resumeconditions; no blanket attribution orwaiver.
- M6.3b functionallease/generation/EOS/pause/boundedcancellation checks passallplatforms (lifecycle/ownership andeos-fix). Sameprocess100heldlease/reconnectcycles allfunctionalpass; LinuxRSS0.142/Android-0.509KBcycle pass, Windowsprivate82.528FAILS16, handles/threadsflatall. Evidence lifecycle/lease-cycles. Windowsmemory portion remains suspended under same dependency;M6.3unchecked.
- M6.4 existingpolicy8testsperplatform pass, plusWindowsD3D11normal/death/controlledGPUwait3/3. Faultscope in fault-coverage. Linux/AndroidEGLcontrollablewait injection unavailable77, notpass; rawnegativepreserved. M6.4b/c GPU-delay subset SUSPENDED pending supportednativefenceinjection; D2publicdefaultchoice unresolved, nochange. Existingpolicies testedexplicitly, no newlyapproveddefault inferred.
- M6.5a/b/d,6b,7a/b validated within documented scopes. Gate runner24bcd78: Linux/Android19pass1skip, Windows20pass; final7c waits8/9.
- M6.8a complete: allocation/map/streaming/cached and GL-staging diagnostics archived in cpu-path/README.md. Staging no-rebind negative retains stale pixels; existing native-fence benchmark does not reproduce it. Full-frame visibility regression e7bde0f passes Linux/Android with corruption detection.
- M6.8b partial: 6275fe5 implements explicitly selected Linux BGRA/RGBA gpu-readback with adopted allocation and both-endpoint COPY|READBACK permission. Default/native paths unchanged. Linux9affected+2finaladapter, Windows9affected, Android6affected andbothABIbuilds passed. Each trace records32 GPUcopy and32readback events,663552000bytes each. Initialcoldmax115.609ms andfinalcoldmax77.604ms retained; no budget/ratepass inferred. Evidence: cpu-path/EGL_READBACK.md.
- e6a7acf adds real cross-process cached shared-CPU comparison:32/32exact BGRA/NV12 frames at1080p/4K. This is an allocation control, not a FrameLink shared-CPU path. No executor remains running; all commands/results collected. SharedCPUv1 descriptor nowimplemented: independenttagged128bytecodec, bounded/nonoverlappingplanes, unchangedlegacyprotocols; C11+1024mutation+policynegatives andexistingcontracttests7/7each Linux/Windows/Android pass; bothAndroidABIbuilds. Internal Linux/Android stable-size allocator/import nowvalidated: trueROexports, grow/shrink/seal-listchecks, retainedmappingownership, SCM_RIGHTS BGRA/NV12pixels and100cleanFD/VMAcycles. InitialAndroid write-sealedO_RDWRreadonlymapfailure fixedbytrueROreopen; originalnegative retained. Linux3/3, Androidruntime+dualABIbuildpass. Internal coordinator pool preflight nowvalidated: sessiongeneration/geometry, uniqueIDs, aggregate1GiB/2..16slots, queuedepth andboth-endpoint COPY/CPU-only policy. Linux6/6, Windows5/5, Androidruntime+dualABIbuildpass; cpu-path/SHARED_CPU_POOL.md. This is not a live channel/negotiation capability. AtomicLinux/Androidimportedpool nowjoins preflight+actualFDidentity/rights: duplicatebackingobjects rejected, all-or-nothingadoption, strictsessionreplacement, retainedoldmappingprotection withbounded256weakidentityregistry andBUSYrecovery. Linux5/5, Android2runtime+dualABIbuildpass; cpu-path/SHARED_CPU_IMPORTED_POOL.md. No productionbackend advertises it yet. ExplicitinternalCPUcontrol nowexchanges FCH1 exactsession/role/bothendpointpolicy andFCP1 descriptor+SCM_RIGHTS pool, failclosed nolegacyfallback; realforkedpixelidentity and80malformedFDcleanupcases pass. Linux7/7 Android2runtime+dualABIbuildpass; cpu-path/SHARED_CPU_CONTROL_SETUP.md. No publicfactory selects it. ExactFCA1 poolacceptance nowrequired beforeacceptedstate; malformed/stale/FD/silentpeer negatives pass, Linux7/7 andAndroidruntime+dualABIbuild. ACKsendfailure leaves locallyimportedpool owned butconnectionfailed; no mediaallowed. InternalFCF1/FCR1 ready/release nowchecks exactgeneration/slot/sequence andCPUcompletion contract;32forkedfullpixel/PTSframes and11hostilereadycases pass, Linux7/7 Androidruntime+dualABI. CPUrawpointerdiscipline stillcallerowned; notpublicleases. Releasequalification nowpasses16slotsaturation/reverseorder and15hostile/replay/death/silentheldcases, Linux8/8 Androidruntime+dualABI; no productionfixneeded. PublicPOSIX CPU-only opaquechannel/lease API nowimplements explicitconnectedfactory, reservedwrites/cancel, retainedreadfinalrelease andclose-heldmappinglifetime; nativeGPUtypes separate/noexports. Linux12/12 Androidpublic+C11runtime+dualABIpass; cpu-path/SHARED_CPU_CHANNEL.md. Publicreconnect100freshgenerations eachLinux/Android nowpassesheldoldpixels/newpoolisolation andexactFD/VMAcleanup; dualABIbuilds, cpu-path/SHARED_CPU_RECONNECT.md. Caller/coordinatorstillmustvalidateepochfreshness. Strictopt-inmetadata factoryEx mode nowpreservesDTS/duration/discontinuity/mediageneration, rejectsmismatchedpeers/malformedmetadata withoutdowngrade; Linux13/13 Android3runtime+dualABIpass, cpu-path/SHARED_CPU_METADATA.md. OrderedCPU EOS nowvalidated: strictboth-peeropt-in FCE1generation/finalsequence, retainedreadsdrainafterterminalmarker, emptystream/disconnectdistinction/sevenhostilenegatives, Linux16/16 Android6runtime+dualABIpass; cpu-path/SHARED_CPU_EOS.md. Explicitcoordinator CPU FDdelegation nowvalidated: exactgeneration/role singleclaim, cancelsblockedsetup/claimedI/O withoutfreeingleases, ownerclose shutsdownduplicates. Linux19/19 Android8runtime+dualABIpass; cpu-path/SHARED_CPU_COORDINATOR.md. Explicitframelinksrc shared-cpu copyout nowvalidated forbroker-boundvideo-only sessions: actualCOPYchecked, paddedplanes/exactpixels/metadata/sharedclockmapping, EOSvsdeath andboundedcancel. Linux25/25 Android13runtime+dualABIpass; finalsourcefocusedtests/trace17copies13824bytespass. cpu-path/SHARED_CPU_GST_SOURCE.md. ExplicitCPU sinkcopy-in nowvalidated withactualCOPY/SystemMemoryonly, boundedqueueownership, terminalleasedrain andcancellation. RealvideoCPU sink/source BGRA/NV12 roundtrip passes; Linux26/26 Android14runtime+dualABI,100roundtriprepeats; finalaccountingfocusedLinux/Androidpass. cpu-path/SHARED_CPU_GST_SINK.md preservesinitialunlabelledtestfailure/fixturecorrection. CPUauthenticatedevents nowjoinexactafterFrames/PTS GAP/barrier/END; no skippedbarriers, mappedGAPs, boundedFLUSHcancellation via nonconsumingreadinesswait. Linux27/27 Android15runtime+dualABIpass; finaldeadlineclamp3focusedtests/passbothplatforms, cpu-path/SHARED_CPU_GST_EVENTS.md. A/Vterminalcoordinatorprerequisite nowtested: empty/nonempty idempotentobservations, contradictorycounts/PTS, replacementreset/stalewait andboundedcancellation; Linux3/3 Android3runtime+dualABIpass, cpu-path/AV_TERMINAL_PREREQUISITE.md. IntegratedCPUvideo+authenticatedPCM nowpasses exactBGRA/NV12pixels/PCMbytes, commonmapped37msoffset, bothEOSsubmissionorders/emptyaggregateEOS, terminalconflict/PCMdeath andboundedteardown. Linux5/5 Android5runtime+dualABIpass; cpu-path/SHARED_CPU_AV.md. A/Vrequiresauthenticatedeventstream; brokerprovidedFDsonly. Explicitshared-cpu binselection nowvalidated: same6integratedcases throughbothbins plus5configurationnegatives; Linux6/6 Android6runtime+dualABIpass includingexistingnative/localbinregression, cpu-path/SHARED_CPU_BINS.md. CPU session=discovery stillrejects, nofallback. CPUbootstrapSTREAMauth prerequisite nowimplemented: separatecredentialdomain/fullconfig/freshVrole, wrongkey/epoch/generation/clock/crossdomain/deadline/cancel negatives; Linux3/3 Android2runtime+dualABIpass, cpu-path/CPU_BOOTSTRAP_AUTH.md. AuthenticatedUnixSTREAM endpointtransfer nowimplemented: freshSEQPACKETpair/exactsingleSCM_RIGHTS/CLOEXEC/ACK/wholedeadline/epochchecks; wrongtype/missing/multiple/truncatedFDs/marker/silentpeer rejectwithexactFDcleanup. All12thin/binA/Vcasesnowusetransferredendpoints. Linux3/3 Android2runtime+dualABIpass, cpu-path/CPU_ENDPOINT.md. ProtectedtypedUnixdiscovery74f6428 nowvalidated: independentpublicsocketname/privatecredential,FAR2notFAR1,ownerexclusion/crashrecovery; Linux4/4 Windows3/3 Android3runtime+dualABI, twoAndroidfilesystemfixtureexclusions documented. LocalCPUsetup2df5ce4 nowcomposesdiscovery/auth/clock/PCM/events/endpoint/pool;16A/Vcasesincluding4localsetup,5negativecases, Linux5/5 Windows3/3 Android4runtime+dualABIpass. DirectCPUbin session= nowvalidated:20integratedA/Vcases plusbounded60s-discoverycancellation; Linux5/5 Windows4/4 Android4runtime+dualABIpass. IndependentLinuxprocessBGRA/NV1264x64smokes deliver32exactframes+25920stereoPCMframes each, source/receiverSHA256andlengthsmatch; nofullresolution/rateclaim. Evidence LOCAL_CPU_BINS.md. Allcommandscollected. Explicitshared-CPUbenchmarkselection nowvalidated: Linux/Android BGRA/NV1264x64 each150frames/240000PCM, exactly150copyevents perendpoint andexpectedbytes; Linux3/3 regressions,Windows2/2,dualAndroidABIbuilds,5invalidoptionnegatives. Evidence cpu-path/SHARED_CPU_BENCHMARK.md. 1080p60BGRA preflight300/300video+240000PCM passed,zero17msmisses,commonAVmapping0ns. First sustained sharedCPU cell validated: Linux BGRA1080p60 1800s revision65a1268,108000video/86400000PCM,180000contiguousaudio,zero17msmisses,arrivalp956.683/max11.909ms,AVmapping0ns,consumer/producerRSS3.23KBmin each<64,CPU24.08/33.61%. Archived cpu-path/shared-cpu-benchmark/linux-bgra-1080p60-sustained. Three interrupted gateway attempts preserved; dynamic awaitResults has verified600000ms hard timeout, so use native fallback for1800s. NV121080p60 5s preflight passed300video/240000PCM. NV121080p60 sustained collected and validated:108000video/86400000PCM,180000contiguousaudio,zero17msmisses,p952.504/max4.297ms,AVmapping0ns,RSS3.23KBminboth,CPU9.62/13.07%; archive linux-nv12-1080p60-sustained. LinuxsharedCPU3/4 cells. BGRA4K30 sustained validated:54000video86400000PCM,180000contiguousaudio,zero34msmisses,p9524.317/max27.684ms,AVmapping0ns,RSS3.23/3.24KBmin,CPU35.32/45.96%; archive linux-bgra-4k30-sustained. NV124K30 sustained validated:54000video86400000PCM,180000contiguousaudio,zero34msmisses,p9510.382/max23.787ms,AVmapping0ns,RSS3.24KBminboth,CPU17.99/25.44%; archive linux-nv12-4k30-sustained. LinuxsharedCPU4/4 sustained cells nowvalidated, same65a1268binaries. Native handle expired but durable exit0/analysis0/summary collected; no rerun. AndroidBGRA1080p60 preflight countspass300video240000PCM but timingFAIL20/300 over17ms,p9519.511/max37.950ms,CPUconsumer97.36%; preserved android-bgra-1080p60-preflight. BGRAendurance suspended: Evelyn owns bounded copy/consumer profiling, no waiver. MatchedNV12 formatcontrol300video240000PCM passeszero17msmisses; AndroidNV121080p60 sustained validated108000video86400000PCM,zero17msmisses,p9512.032/max16.376ms,AVmapping0ns,RSS-75.10/-86.40KBmin,CPU61.19/30.76%. NV124K30preflight counts150/240000pass butfirst3framesFAIL34ms,max48.379; noendurance. BGRAtrace300exactcopies each hasduration0, notcopytiming; localBGRAcontrol300/240000passzero17msmax0.99ms. Next bounded allocation/zero/row-copy isolation onAndroid; no productionchange. Evelyn owns BGRA/4Kstartup diagnosis, no waiver. Nativefallback retained due verifiedgateway600s limit. Windowsallocator stillpending, thenbin/A-V/rategates. M6.8b/c andM6.9 remain open.


M6.4a default-policy decision unresolved;M6.5–9 pending. No automatedstatus reports. Notify Jon atfullM6closure andholdbeforeM7 for maintenance.
