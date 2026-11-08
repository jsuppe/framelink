# CPU bootstrap authentication prerequisite

Adds fl_gst_session_authenticate_cpu_bootstrap for a distinct connected STREAM.
It derives a separate credential from the root32-byte secret and entire canonical
session wire configuration, using label `FrameLink public CPU bootstrap session v2`
(including terminating NUL). Fresh directional challenges use role V. Failure
closes the stream and cancels the session; deadline/caller cancellation are bounded.
Success leaves the authenticated stream owned by the caller, with no channel
publication, no event reader, no payload, and no FD transfer.

This does NOT authenticate an arbitrary subsequent socket or constitute completed
CPU rendezvous. The next step must transfer the actual SEQPACKET endpoint over
this authenticated connection with strict ancillary-FD validation before binding.
Discovery/local session= support remains unavailable for CPU mode.

Tests cover success/usable stream, wrong root key, generation/epoch/clock mismatch,
event/audio cross-domain peers, silent deadline and caller cancellation. Linux3/3
(including base av_auth) and Android2 runtime tests pass; both Android ABI builds
pass. Exact commands/hashes/logs in cpu-bootstrap/final/.

Initial test failed because the common auth role allowlist did not yet include V;
registered the new role and retained that failure under initial-role-rejection/.
A subsequent validation script requested fl_av_auth on Android, where that target
is not configured; retained unknown-target result under auth-target-scope/.
The Linux av_auth regression passed. Android validation uses the session transport
binary (same auth implementation) and integrated CPU A/V binary instead; no absent
target counted as a pass. No Windows validation or final M6 qualification claimed.
