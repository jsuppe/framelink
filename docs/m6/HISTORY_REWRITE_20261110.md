# M6 history rewrite, 2026-11-10

The 151 commits after `f633c75` (the last commit published on
`origin/feature/m6-qualification`) were rewritten before their first push so that
machine-local home directories, user names and host names do not appear in
public history. Each file was passed through the `localenv` clean filter
(see [LOCAL_ENV.md](../LOCAL_ENV.md)); with a matching `local.env`, checking a
rewritten commit out reproduces the original bytes. Three whole-machine process
listings and one regenerable GStreamer registry cache were removed:

- `windows-integrated-04babab/local-before.txt`
- `windows-integrated-4a49a92/local-processes-before.txt`
- `windows-integrated-8025ddd/local-processes-before.txt`
- `windows-preconnect-negative-control/corrected/m5-preconnect-uem5qcaf/plugin-registry.bin`

Commit messages had short hashes updated automatically. Evidence files, the plan
and agent notes still cite the **original** hashes; use this table to translate.
`f633c75` and everything before it are unchanged. The original commits remain in
the local branch `backup/m6-bounded-output-unredacted-20261110` (not pushed).
Checksum manifests inside evidence folders describe the unredacted files, as
they verify in a configured checkout.

| original | rewritten | subject |
|---|---|---|
| `fda55c7` | `438dd60` | test(windows): verify GstMemory reservations across process relay |
| `f8f2483` | `a5e907b` | fix(gst): serialize concurrent producer audio/video EOS collection |
| `f859d0d` | `9a726b7` | Add typed protected CPU control discovery and collect Windows gate |
| `f642162` | `06a3ac5` | feat(m6): validate Windows shared CPU section allocation |
| `f2aba61` | `0a31e36` | Complete Android CPU cost isolation without waiving target failures |
| `f291ffd` | `51ff2d7` | test(windows): pin native receiver relay rejection contract |
| `ec000ae` | `fba33c2` | Add explicitly negotiated ordered EOS to shared CPU channels |
| `e7bde0f` | `700b714` | test(m6): verify all pixels after repeated CPU writes and GPU reads |
| `e6a7acf` | `3943399` | docs(m6): compare real cross-process cached CPU allocation |
| `e035cc9` | `aa42493` | docs(m6): isolate device churn and test ownership-preserving teardown alternative |
| `db9c836` | `4e60a9a` | docs: qualify Android NV12 and preserve 4K startup failure |
| `da12146` | `7202742` | Add bounded current-user Windows CPU endpoint prerequisite |
| `d9585a8` | `4152100` | Wire policy-bound Windows native producer through AV bin |
| `d8f97c2` | `c4b9583` | Expose additive Windows CPU pipe factories with shared opaque leases |
| `d85be55` | `fe9bd51` | Bind explicit shared CPU channels to coordinator generations |
| `d8023c4` | `f13c1cb` | Prepare CPU consumer mappings and retain zeroing only for padded outputs |
| `d6f9b4b` | `26e2ff9` | Validate Android BGRA retention endurance and advance NV12 gate |
| `d60f683` | `2c1b757` | Enable protected local Windows CPU A/V bin sessions |
| `d514fc7` | `b63de9a` | test(m6): isolate local consumer lifecycle resource costs |
| `d0a6087` | `53bcfac` | docs(m6): record owned Windows AV integration gate and next delegation work |
| `cf7408d` | `718125f` | Enable broker-bound Windows shared CPU GStreamer elements |
| `cf00c64` | `30bc74a` | Record Windows sustained collection and native policy validation handoff |
| `cde2f0d` | `84d997c` | feat(windows): compose authenticated local native session setup |
| `ccd0460` | `4c89c42` | test(windows): bound producer death across checked native relay |
| `cc7e8d9` | `f084987` | Add Windows CPU ownership, metadata and ordered EOS protocol |
| `cbbed78` | `94338c7` | docs: preserve cached-memory control evidence separately from setup protocol |
| `c8ff226` | `0879f5a` | feat(windows): expose authenticated native session pipeline epoch |
| `c597cba` | `0330c7a` | Implement broker-bound Windows CPU pool exchange and exact ACK |
| `c5653be` | `c8b3c19` | feat(m6): validate atomic Windows CPU pool adoption |
| `c5382ab` | `d9b220a` | docs(m6): record task-cache repair and owned Windows regression |
| `c1a517f` | `adde942` | feat(session): authenticate exact native memory policy agreement |
| `bfa0d3f` | `7806d24` | test(windows): cover coordinated native receiver AV ownership |
| `b997e7c` | `56bbc92` | feat(m6): add bounded Windows CPU pipe transport |
| `b97fb99` | `2ee33ab` | feat: require bounded shared CPU pool acceptance before ready state |
| `b4eb366` | `013c282` | feat(windows): support explicit native receiver in local AV sessions |
| `b1ed9da` | `c6eb77b` | docs: complete Linux CPU matrix and retain Android timing failure |
| `b18c7d0` | `af125ad` | Record remaining Android CPU AV target failures after NV21 optimization |
| `b15f220` | `309cfe5` | Record Windows CPU target preflight and owned sustained validation |
| `b0cad2f` | `9bb617a` | Bound checked PCM relay cancellation waits and verify both session endpoints |
| `afadfa5` | `9b22202` | Enable explicitly bound CPU video in authenticated A/V sessions |
| `ad3f127` | `9ecc14e` | fix: reduce implicit trace allocation and verify bounded capacity |
| `a9e6255` | `93dc644` | test(m6): verify GPU delay injection and retain unavailable EGL coverage |
| `a9cb551` | `00367c4` | Record Android source and local consumer cost controls |
| `a9a55a3` | `36e2da9` | test: record Android native GPU and PCM identity joins |
| `a712ac5` | `1a64c61` | test: qualify shared CPU release replay and held-slot failures |
| `a6da0d9` | `f4cc1dc` | Archive NV12 4K retention gate and start matched affinity diagnostic |
| `a6b05da` | `c937558` | Cache native Windows producer layout per ring generation |
| `a646009` | `193ddff` | Forward authenticated relay events with count checks and safe cancellation drain |
| `a2d25b4` | `f97cd17` | docs(api): refresh verified candidate and local native contract |
| `a26a628` | `3c636b5` | Record native bin validation and owned Windows integration gate |
| `a0b1c09` | `2f89a70` | Expose explicit broker-bound CPU selection in A/V bins |
| `a0567ec` | `36063b1` | docs(m6): qualify readback controls and expose EGL refresh gap |
| `a0148d1` | `e09c851` | docs(m6): preserve Windows producer lifecycle resource failure |
| `9f6bad2` | `79903a3` | Add explicit shared CPU copy-out to coordinated video source |
| `9d113ef` | `1e01707` | feat: opt into bounded ordered PCM relay GAP forwarding |
| `9b6fab5` | `4d2f3fc` | Record Windows local CPU integration and owned exact-revision gate |
| `9ac25ea` | `920da86` | feat: validate shared CPU pool against session and endpoint policy |
| `99ecc71` | `a1cd3cc` | Archive affinity control and advance packed consumer copy diagnostic |
| `99a8318` | `ec40fc6` | docs(m6): reject packed-copy control and track NV12 residual diagnostic |
| `99a6c47` | `fec645b` | Record renderer dependency recovery and reject truncated timing capture |
| `97f8be1` | `46db177` | Preserve BGRA residual failure and hand off bounded stage diagnostic |
| `97afa90` | `4855608` | test: qualify public CPU reconnect with retained old-generation leases |
| `93283ac` | `8a5f010` | Avoid retaining copied CPU inputs in AV sink bin; qualify release lifecycle |
| `8edab26` | `3dd19b8` | docs(m6): collect Windows result and hand off native agreement integration |
| `8c408eb` | `fbb49f1` | Authenticate CPU bootstrap streams with separate session-bound credentials |
| `877e78c` | `2c8e056` | feat: expose explicit CPU-only channel with retained read and write leases |
| `8583411` | `def9057` | feat(gst): forward broker memory policy through receiver bin |
| `8539340` | `b0b03cd` | docs(m6): pin Windows API candidate and exact-head checks |
| `8494dc8` | `cd8ddf7` | test: retain Windows GPU joins and scoped renderer boundary audit |
| `825cca1` | `a87074f` | Record named Windows CPU integration and owned clean gate |
| `81996d0` | `eabb9b0` | docs: qualify sustained BGRA shared CPU 4K30 |
| `816bbe2` | `b61acd1` | Trace CPU allocation copy and Android normalization stages |
| `8025ddd` | `dbee0ac` | feat(windows): declare render-producer usage for coordinated decoder AV |
| `7ff246e` | `aa440a9` | Archive NV12 residual diagnostic and advance Linux final-revision rate gate |
| `7f9bc2c` | `9af723b` | docs(m6): record clean local lifecycle baseline and isolate D3D11 costs |
| `793bc44` | `592d0bb` | Preserve Linux CPU target losses and readback stage costs |
| `76a65d2` | `5d57065` | Enable Windows shared-CPU benchmark and independent-process coverage |
| `7642b6f` | `95c769f` | Archive residual stage diagnosis and advance production BGRA rate gate |
| `74f6428` | `bb135bf` | Add typed protected Unix rendezvous for explicit CPU sessions |
| `71822f8` | `35224a9` | Record owned Windows authenticated-pipe integration gate |
| `70d8407` | `128afad` | Optimize Android NV21 CPU normalization with guarded bulk rows |
| `6d7e6be` | `47d7d21` | test(windows): cover native session teardown with retained producer memory |
| `6c8ff2d` | `d519f10` | Record Linux native stage diagnostics and rejected short CPU setup |
| `68e5c23` | `46c9277` | test(windows): verify real decoder buffers obey strict producer contract |
| `68a92bd` | `5013de3` | feat: preserve CPU media metadata through strict explicit negotiation |
| `67c1523` | `63ba1ca` | test(windows): qualify broker-bound shared CPU audio video bins |
| `66ee279` | `804e6ea` | feat(memory): add sealed shared CPU allocation with retained mappings |
| `65a1268` | `cd89f58` | Add explicit shared CPU benchmark selection and copy accounting evidence |
| `6522efe` | `d5a560c` | Compose protected Windows local shared CPU A/V sessions |
| `6275fe5` | `d57a55e` | feat(gst): add explicitly negotiated Linux GPU readback path |
| `609cb74` | `23e9302` | fix(windows): reject trailing IPC message bytes |
| `5e62bdd` | `61729f3` | test: distinguish source and post-push stall signatures |
| `5d673e0` | `aa3f09a` | docs(m6): retain Windows lifecycle resource failure and isolate harness costs |
| `5863056` | `3962ac5` | docs(m6): record preconnect correction and owned Windows verification |
| `57b159a` | `e1ee430` | feat(memory): add explicit bounded shared-CPU descriptor contract |
| `555d366` | `24d3be2` | Prepare shared CPU sink pages before media readiness |
| `548fe19` | `56df8c4` | Require bound memory policy for coordinated native producers |
| `51248d1` | `2bff01d` | docs(m6): retain negative DXGI trim lifecycle evidence |
| `4f816e6` | `c7a3ab3` | Guard coordinated native relay frames and preserve cancellation drain ownership |
| `4f7a61a` | `ecae8b9` | docs(m6): validate Linux lifecycle endurance in both restart directions |
| `4f37f25` | `f150b15` | gst: reuse bounded CPU output buffers for shared CPU sessions |
| `4a49a92` | `df51102` | feat(gst): expose explicit video overload controls on AV producer bin |
| `49ebaf9` | `540d3e9` | feat: track explicit CPU ready-release slot ownership |
| `48d59f1` | `e7d2370` | test: record Linux native GPU and PCM identity joins |
| `479d955` | `ccc6e19` | docs(m6): reconcile current CPU path execution ledger |
| `4761f69` | `bd32b1a` | fix(samples): reject silent PCM relay event loss through EOS |
| `45299de` | `25762b2` | Record exact Windows CPU API integration gate and execution owner |
| `448251e` | `cc7279f` | docs: qualify sustained NV12 shared CPU 1080p60 |
| `4326c61` | `e2b111d` | feat: negotiate explicit shared CPU setup and transfer checked pools |
| `431ad3c` | `eb58597` | docs(m6): hand off exact Windows API integrated verification |
| `40f2fed` | `33a5fad` | docs: publish measured platform limits and open acceptance gaps |
| `3f45f7b` | `1684576` | Compose authenticated Windows named CPU session provisioning |
| `3ca89e5` | `e5c910e` | Authenticate Windows CPU session binding on the exact pinned pipe |
| `3bb31b4` | `4ebafa8` | Validate source-time contract before coordinated Windows relay forwarding |
| `3ab586a` | `98fd48a` | test: quantify Linux trace off-on timing and resource deltas |
| `3916f9b` | `9d13b26` | docs(m6): hand off receiver policy integrated Windows verification |
| `34a391d` | `96df129` | test(windows): separate preconnect fault control from normal late budget |
| `32963c5` | `a3a5ca6` | fix(samples): interrupt PCM terminal waits on either session failure |
| `327678b` | `94dd24e` | Transfer CPU endpoints over authenticated Unix bootstrap streams |
| `30d7a53` | `274bf5a` | test(windows): drain idle Gst tasks before ownership sampling |
| `2e96a9e` | `c74aa0a` | Preserve NV12 1080 timing failure and advance 4K retention gate |
| `2df5ce4` | `e31f594` | Compose authenticated local shared-CPU session setup |
| `2ab06fb` | `3e798df` | test: record Android trace off-on resource and timing costs |
| `293820e` | `c98d358` | feat: stage shared CPU pool imports with retained generation identity |
| `271c48b` | `29c30f9` | Integrate explicit Windows native candidates into automatic AV bins |
| `25d633f` | `3ed244d` | docs(m6): record owned full Windows regression handoff |
| `24bcd78` | `6b8235a` | test: publish serial cross-platform M6 gates with strict outcome accounting |
| `23dcf44` | `2b11c14` | test(m6): measure same-process lease lifecycle resources without waiving failures |
| `23db5ea` | `2bb9ea1` | Add explicit CPU sink copy-in and qualified video roundtrip |
| `230159c` | `3b29fcd` | Record bounded Windows stage costs with explicit capture limitations |
| `1fdbaa6` | `b307ccf` | fix(samples): observe both sessions during native event waits |
| `1fb7280` | `d5d2345` | docs(m6): hand off native epoch integrated Windows validation |
| `1b0357a` | `e4e507e` | Publish scoped software stage costs and complete renderer submission trace |
| `18f0847` | `58e0e85` | docs(m6): preserve Windows integrated lifecycle failure |
| `174b345` | `b3e6ab6` | Qualify coordinator terminal arbitration before CPU A/V integration |
| `16ddeba` | `dd780a1` | docs: qualify first sustained shared CPU rate cell |
| `1460444` | `cd68477` | test(windows): integrate decoded reservations through process relay |
| `14096e3` | `bf42977` | Add checked bounded PCM relay and authenticated A/V terminal join |
| `1288f66` | `f525337` | ci: include deterministic M6 ownership timing and trace negatives |
| `0fac66a` | `dad15c9` | Record owned 718125f Windows API regression handoff |
| `0f30151` | `cd5f244` | Enable explicit authenticated shared-CPU discovery in A/V bins |
| `0d2e40c` | `470f74b` | test(windows): integrate same-allocation relay with coordinated PCM and events |
| `0cd2ddd` | `fe37c8c` | test: document Windows trace overhead and explicit flush costs |
| `0bd7864` | `445a708` | Join CPU video with authenticated ordered event controls |
| `06eee10` | `ca8a076` | docs(m6): collect 4c89c42 integrated Windows validation |
| `0677ba8` | `d5b270f` | docs(m6): record owned Windows coordinator regression handoff |
| `0614429` | `7f93047` | test(windows): qualify native producer bin with coordinated PCM |
| `04e8a90` | `fb23f4e` | docs(m6): qualify Android lifecycle and retain Windows resource blocker |
| `04babab` | `7e5b5ce` | test(windows): qualify checked native PCM relay across three processes |
| `02509f7` | `e0994d0` | feat(windows): bind shared CPU channels to session coordinator |
| `010346d` | `fa1374b` | docs(m6): reproduce lifecycle growth in isolated D3D11 device control |
