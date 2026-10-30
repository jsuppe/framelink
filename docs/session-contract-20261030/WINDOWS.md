# M2 Windows session-contract qualification

Source `0a5ba51d6a578e9c05027a96220ab225939358e2`, Caspar Windows x64,
MSVC19.44.35216.0 / Visual Studio2022, Release; isolated directory
`C:\Users\jonsu\dev\framelink-session-20261030`. Existing live demo untouched.

- All targets built; **32/32 full regression tests passed**.
- `session_contract` independently passed: exact capability intersection,
  48kHz preference and fallback, rational frame-rate equivalence, malformed
  capability rejection, version/generation/config validation, canonical codec,
  every truncated wire length, malformed reserved/version/magic bytes,
  negative/overflow clock mapping, uncertainty thresholds and cumulative PCM timing.
- A true C11 consumer compiled with MSVC (`LanguageStandard_C=stdc11`), linked
  the FrameLink DLL and ran successfully. It includes the public session header,
  validates config, round-trips its codec and computes a sample-index offset.
  `_Static_assert` is included. This is not merely a C++ header compile.
- No portability source changes were needed. These validate the M2 pure contract,
  not cross-process session orchestration, audible playback, drift correction
  or Android sandbox deployment (later milestones).

Build logs and test output are retained in `windows-*.txt`. To reproduce, build
FrameLink normally with `cmake -S . -B build -A x64` and
`cmake --build build --config Release -j 8`. Put the installed GStreamer `bin`
and FrameLink `build\Release` on PATH, then
`ctest --test-dir build -C Release --output-on-failure`.
For the C11 check, copy `c11-probe/` to the repository root beside `include/`
and `build/`, configure/build it with CMake/MSVC and run `session_c11.exe` with
FrameLink's DLL on PATH. Source revision above applies to these results;
subsequent review changes require a targeted rebuild/rerun.

## Final review gate

Tested reviewer source `fddbf7c1d9f1a6f4ec0d73629df368d5a2e4d1e3`, including
`flSessionValidateSelection`, stable track-identity replacement rules and new
C11/independent-peer fixtures. All targets rebuilt successfully in the same
isolated directory; **34/34 full regressions passed**. Focused verbose run:
**3/3 session-contract tests passed**, including the checked-in MSVC C11 API
consumer and independent proposer/acceptor processes via Python3.13 pipes.

Peer cases passed: agreement; rejection of unoffered selection, stale generation,
changed track identity, unknown clock and malformed proposal; proposer rejection
of changed acknowledgement. This tests contract agreement through bounded test
pipes, not production rendezvous/security or a new media transport. Focused tests
were also run separately to retain each expected exit code in
`windows-final-focused.txt`; full suite and rebuild logs accompany it.
