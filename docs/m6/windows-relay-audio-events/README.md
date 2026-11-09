# Checked PCM relay: fail closed on unsupported serialized media events

Owner: Evelyn. Windows-first M6 repair; not full relay or API acceptance.

## Evidenced defect and contract

The sample-only appsink pull discarded serialized GAP events before exposing the
next sample. An initial GAP negative failed on the prior b0cad2f implementation:
`forward()` returned success instead of refusing publication. After changing the
pull to inspect objects, a separate authenticated terminal negative demonstrated
that `gst_app_sink_is_eos()` does not prove the serialized-event queue is drained:
`finish()` could accept a GAP immediately before EOS. Both negatives are preserved.

The checked sample adapter now inspects serialized objects. Stream-start, caps,
segment and tag setup are consumed; every sample still validates its own exact
caps/segment/timestamps. GAP and other unsupported serialized media events return
INVALID, cancel the downstream session and permanently stop PCM publication.
No synthetic silence, hidden timestamp rewrite or successful GAP forwarding is
claimed. Applications requiring GAP forwarding cannot use this sample adapter
as a complete implementation yet. Existing explicit buffer DISCONT is preserved.

`finish()` checks residual queued objects before authenticated terminal publication.
A call examines at most 32 objects, returning retryable TIMEOUT on the bound;
uninspected events are retained. No pending sample is discarded. The caller must
serialize use, preserve the broker-authenticated port binding and drain native
leases independently, including after cancellation. Public C/wire layouts,
GStreamer properties, overload defaults and fallback selection are unchanged.

## Verification

The final diagnostic exercises initial and midstream GAP with a following sample,
plus GAP immediately before EOS through the authenticated audio path. It checks
no following sample/accounting advance, permanent failure and downstream session
cancellation. Existing empty/nonempty A/V, exact PCM/pixels/37ms timing, DISCONT,
queue saturation/retry, bounded cancellation and held-native-lease checks remain.

Focused Windows build/diagnostic/regressions and file hashes are archived alongside
this file; see `final/status.json` for actual results. These sample headers are
currently included only by the Windows test target, so no deferred Linux/Android
campaign was launched. API remains a candidate, not frozen; M6 open; M7 on hold.
