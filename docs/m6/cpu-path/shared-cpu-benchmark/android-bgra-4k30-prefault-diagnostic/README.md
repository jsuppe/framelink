# Android BGRA4K30 diagnostic — FAIL retained

555d366/binary8266261c,1800s,exit0/pull0/analysis1.54000video86400000PCM180000contiguousaudio;15video34msmisses:2atstartup and13at229–369s. max44.556ms,p9524.130ms. Audiozero misses, inferredAVmapping0ns. RSSnegative, handles/threadsbounded. Startup exclusion would NOT fix acceptance. No qualification.

Next10s diagnostic temporary instrumented build shows first consumer copy27.974ms vs later~12ms; first zero8.173ms vs~4ms; sinkfirst8.182ms vs~6.8ms. Consumer receive/map116us and output acquisition/map16us on firstframe are small. These are wall times, not proof of faults or memcpy alone. Instrumentation restored out of repository source; exact diagnostic binaries/source hashes in <linux-home>/dev/evidence/m6-cpu-path/android-4k-stage-probe. Next bounded450s adds thread minorfault/CPU-time counters to separate copy/pagefault work from descheduling, includes previously observed late window. No production change or budget waiver.
