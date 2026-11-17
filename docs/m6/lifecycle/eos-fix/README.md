# Serialized producer A/V EOS: affected validation

Linux, Android arm64 and Windows each passed twelve focused concurrent-EOS pairs and the full producer-bin test (including four barrier pairs, startup ordering, pause, drain, teardown and negative paths). Source hashes are recorded in status files. The barrier holds each track at its child sink EOS event until the other arrives; checks retain exact frame/audio counts and successful consumer drain. Parent message forwarding occurs outside the session mutex. Production device lifetime and resource gates are unchanged.

Original Android SIGABRT evidence is retained in ../android-eos-failure. A sustained Android lifecycle rerun follows on the fixed revision; these affected tests alone do not qualify lifecycle endurance or resolve the Windows memory slopes.
