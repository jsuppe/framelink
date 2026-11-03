# Bounded broker/STREAM adoption validation — 2026-10-31

Source: `6b371950492a15601051a30f26517ebc64afb043`, based on accepted M5
performance checkpoint `b09b297`. Integrates/reviews the old `2e8a6cf` adapter,
not its permissive million-frame/hour-long option limits. No renderer change,
benchmark rerun, M5 merge, M5a implementation, or cross-UID A/V completion.

## Contract and lifetime review

- Producer options now match CLI bounds; optional exact mode-3 finite budget
  remains <=300s, independent of 15s default startup and 1–2s socket idle bounds.
  Mode0–3, mutual HMAC, session/generation-derived audio keys, PRIMED/GO ordering,
  pool12/pending6/common100ms remain intact.
- Embedded guard uses GCancellable control/auth I/O and thread-safe appsrc EOS,
  not process `_Exit` or concurrent shutdown/close on one GSocket. CLI hard guard
  remains unchanged. Existing four-argument authenticateServer symbol retained;
  cancellable variant is additive. Owning FD helper and no-provider API unchanged.
- Broker uses signature permission checks, unique owner-token grants, two STREAM
  sockets, replacement/stale-revoke protection, Binder death and shutdown EOF.
  Setup failures close only unstarted server endpoints; started workers solely
  close their own server FDs. Client STREAM arrays close in finally paths.
- Provider callbacks must obey their timeout; native video setup/teardown remains
  bounded by configured timeout. Producer cancellation is not immediate receiver
  notification: legacy streaming without GDP EOS can await its own session
  deadline (5s fixture). Cancellation/deadline tests measure producer completion
  independently. No heartbeat or new PCM protocol was added.

## Checks

Linux Release build succeeded. Eight unique affected CTests passed sequentially:

- `av_connected_fd`, `av_auth`, `av_offer_bounds`, `broker_producer_stream`:
  final4/4, 16.27s, `linux-adapter-auth.log`.
- `m3_av_apps`, `m5_finite_budget`, `m5_startup`, `m5_preconnect`:
  4/4, 85.91s, `linux-desktop-regressions.log`. These ran after the GCancellable
  lifetime correction; the final additive auth-symbol compatibility wrapper was
  then built and revalidated by the four auth/adapter tests above.
- Adapter suite: 14 invalid bounds before resolve; pre-cancelled request; silent
  peer nonfatal100ms deadline; 11 real-peer cases: mode1, mode2 with2.3s prepare,
  mode3 lasting longer than startup timeout, wrong root/audio key, wrong audio
  role, budget mismatch, startup revoke, startup/media cancel, media deadline.
  Real negotiated HMAC/GDP/PTS/PCM flow over Unix STREAM pairs; ordinary local
  video ring. Session/clock/end ordering is checked. Desktop suite covers mode0.
- Full Android native A/V libraries built arm64-v8a and x86_64, API29, NDK28.2,
  Release, current source in fresh `/tmp/fl-m5-adoption-{arm64,x86}` directories.
  Old cached build directories pointed at another checkout and were not reused.
- SDK34/Gradle8.14 signed spike APK build succeeded. `device-final`:10/10 on
  A06B4AABFB07983, SELinux Enforcing. Separate app UIDs logged; broker/client
  signer identical, impostor different (`device-metadata.json`). Broker/client/
  remote-owner death, replacement, active-owner reuse, stale revoke, EOF and
  wrong signer covered. No global logcat clear; per-case markers. All three test
  packages stopped after qualification. No unrelated apps/settings changed.

## Retained diagnostic failures

`device-first` is9/10, not discarded. Its client-process-death oracle incorrectly
required Binder death to win the race against socket EOF. Actual log showed
role exchange then `stream_revoke=transport-end`31ms later. The final JNI probe
reports explicit peer EOF, accepting either legitimate retirement cause; the
separate remote-owner test still requires `stream_revoke=owner-death`. Final10/10
includes that stricter remote-owner proof, rather than treating timeout EOF as
Binder-death success.

An initial Linux harness assertion also incorrectly required the receiver's
whole call to finish inside3s after producer cancellation; existing mode2
receiver behavior waited its own5s deadline. The corrected test separately
measures the producer's bounded return and retains false completion on both
sides. This did not widen production deadlines. Loom pre-edit lookup could not
load its local store (`chromadb` absent); milestone/protocol requirements were
reviewed directly, without blocking this task.

## Reproduction

```sh
cmake --build /tmp/framelink-m5-build -j4
ctest --test-dir /tmp/framelink-m5-build --output-on-failure \
  -R '^(av_connected_fd|av_auth|av_offer_bounds|broker_producer_stream|m3_av_apps|m5_finite_budget|m5_startup|m5_preconnect)$'
cmake -S android/avsample -B /tmp/fl-m5-adoption-arm64 -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=<linux-home>/Android/Sdk/ndk/28.2.13676358/build/cmake/android.toolchain.cmake \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-29 -DCMAKE_BUILD_TYPE=Release \
  -DFRAMELINK_GST_ANDROID_ROOT=<linux-home>/dev/gstreamer-android/arm64
cmake --build /tmp/fl-m5-adoption-arm64 -j4
# Repeat with x86_64 ABI/root and /tmp/fl-m5-adoption-x86 build directory.
gradle -p android/binder-spike assembleDebug -PimpostorStore=/tmp/fl-spike-untrusted.jks
python3 android/binder-spike/qualify.py --serial A06B4AABFB07983 --output /tmp/new-lease-results
```

## Next ready task and limits

After parent review: implement the Android native-video connected-FD ownership
bridge and bind broker-issued fresh leases/secrets into signed producer/receiver
APK JNI SessionProviders, then qualify actual cross-UID video+GDP audio with one
shared epoch and generation-safe death/restart. Current spike transfers role
bytes, not GDP; Linux uses the regular same-UID video ring. Active-token checks
are not a persistent production generation/tombstone registry. AHB ownership,
production policy/versioning and real cross-UID A/V integration remain unfinished
work, not an external/user blocker. Physical display/speaker sync remains
unqualified. No new Windows GPU or physical-output claim.
