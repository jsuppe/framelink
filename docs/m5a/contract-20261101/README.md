# M5a.1 contract validation — 2026-11-01

Portable policy/codec only, not platform allocation or native GStreamer support.
Production ABI: include/framelink_memory.h; wire/policy spec: ../../MEMORY_CONTRACT.md.

- Linux Release exported-library tests4/4: memory policy, C11 caller, unchanged
  session contract and C11 session caller. Logs linux.log.gz.
- Fresh Caspar MSVC2022 build, same four tests4/4. Source copy isolated in
  C:/Users/jonsu/dev/framelink-m5a-contract-20261101; windows.log.gz.
- NDK28.2 arm64-v8a/x86_64 builds targeting existing Android29 minimum: four
  executables each linked to actual FrameLink static library. x86_64 is build
  coverage only, not device runtime. Build logs retained.
- Android35 tablet A06B4AABFB07983: all four arm64 executables passed, android.log.gz.
  No GPU allocation, playback, frame-rate or audio timing qualification implied.
- Linux standalone actual policy implementation with -Wall -Wextra -Werror and
  AddressSanitizer+UndefinedBehaviorSanitizer passed. Command below.
- Full-feature Linux CMake configuration registered new memory tests and existing
  required A/V cases; registration is not execution of the full runtime suite.

Tests include selective fallback, every strict operation negative, required vs
preferred usage, distinct producer/consumer requirements, each resource identity,
unknown flags/versions, malformed sizes and reserved bytes, independent golden LE
vector, unchanged outputs on invalid input and unchanged legacy wire constants.
No production allocator/channel/session/default changes.

Initial Android invocation mistakenly targeted26; existing backend calls require
29 (AHardwareBuffer_isSupported/lockPlanes). Corrected to baseline29. This does
not qualify26 or claim that this task changes the minimum supported Android API.
Loom context unavailable due missing chromadb; manual requirements/spec linkage
is recorded in MEMORY_CONTRACT.md and the task ledger.

Reproduce Linux:
```sh
cmake -S . -B build-contract -DCMAKE_BUILD_TYPE=Release -DFRAMELINK_BUILD_GST=OFF -DFRAMELINK_BUILD_SAMPLES=OFF
cmake --build build-contract --target fl_memory_contract fl_memory_c_api fl_session_contract fl_session_c_api -j4
ctest --test-dir build-contract --output-on-failure -R '^(memory_contract|memory_contract_c_api|session_contract|session_contract_c_api)$'
c++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer -I include src/framelink_memory.cpp test/fl_memory_contract.cpp -o /tmp/fl-memory-contract-sanitized
/tmp/fl-memory-contract-sanitized
```

Windows uses Release configuration and --timeout15, builds with GST/samples OFF.
The hosted workflow and mandatory test-registration check now include both new
memory cases. Hosted CI/merge are later gates; no hosted result is claimed here.

Raw logs are losslessly gzip-compressed; use `gzip -dc FILE.log.gz` to inspect.
