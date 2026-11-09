# Windows integrated regression — b997e7c

Fresh isolated Windows source archive at b997e7cab872b0ec64d54f685db331f5f33e3b94.
Archive SHA-256 is recorded in status.json and checked remotely before extraction.
Release build exit 0; full serial ctest **124/124 passed**, 567.23 seconds.
Completed 2026-11-09T00:20:26Z and collected by the next recovery wake.
No test was repeated to collect this result. Local controller and remote process
inspection showed completion before the next Windows task began.

This validates the existing integrated regression suite, not missing CPU transport,
M6.9 native-producer integration, timing budgets or lifecycle acceptance. Those gates
remain open. Linux BGRA results were already collected; deferred campaigns not run.
