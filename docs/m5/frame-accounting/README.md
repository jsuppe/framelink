# Frame accounting, not average FPS

`tools/frame_accounting.py` consumes identity-bearing JSONL or existing flight
recorder CSV/gz files. It records every observed frame's stages, explicit drop
reason, missing/unclassified stages, startup/steady/end partition, repeated
presentation count, and stage-delay distribution. `--require-complete` fails on
unclassified frames, ambiguous keys, missing GPU completion for any repeated
submission, lost trace records, or anonymous pre-submit deficits. A classified
drop is accounted for, **not** a successful delivery. Complete accounting is not
a zero-loss performance pass.

JSONL fields: `session`, `generation`, `frame_id`, `pts_ns`, `time_ns`, `stage`,
optional `reason`, optional `submission_id` for repeated GPU draws. Frame keys
include generation; resets cannot merge identities. Stages are `sink_receive`,
`buffer_acquire`, `submit`, `ipc_receive`, `consumer_acquire`, `schedule`,
`render_submit`, `gpu_complete`, `present_submit`, `drop`.

CSV supports one generation per channel per capture, with unique PTS per frame.
Duplicate same-stage PTS are flagged ambiguous rather than guessed. A capture
that spans reconnects or PTS resets must be split or exported as generation-aware
JSONL. Renderer event generations must match `--generation`. Render/present
repeats are supported; renderer per-submission IDs match GPU completion. The
renderer instrumentation contract is documented in vkrender's
`docs/m5/FRAME_ACCOUNTING.md` (commit 1217e46).

```sh
python3 tools/frame_accounting.py --csv --generation 2 \
  --output accounting.json capture/*.csv
python3 test/test_frame_accounting.py
```

Default startup/end windows are one second of observed media time at either
end, with startup taking precedence for short clips. They are analysis windows,
not assertions that losses there are acceptable. Same-host steady-clock times
are required for cross-process intervals. Receipt can race submit-return; small
negative intervals are possible and are reported, never clamped. No scanout or
optical/audio-output claim follows from software present submission.

## Saved evidence, not new performance measurements

Input: existing `clock-policy-fix/m5-policy-synthetic-final/1920x1080-60-NV12-av`
CSV capture (old, before the latest fixes). `saved-nv12-60.json` reports:

- 300 anonymous sink wrapper calls; 254 identity-bearing submissions.
- 46 anonymous pre-submit gaps cannot be assigned individual PTS from this old trace.
- 254 submissions and IPC receptions, 177 consumer acquisitions.
- 77 explicit pending-frame replacements: 20 startup, 42 steady, 15 end.
- Missing sink identity and renderer stages remain **unclassified**; full accounting fails.

This identifies the 77 losses from actual replacement flags, not subtraction of
FPS averages. It does not claim that the latest corrected build still loses them.

## Fresh instrumentation validation

Linux Release plugin/shared renderer compilation passed. Existing `fl_gst_sink_stats`
was run against the newly built plugin with `FRAMELINK_GST_TRACE` enabled: 3 input
frames, 3 explicit `no_consumer` drops; the analyzer accounts for all three.
`no-consumer.json` is that fresh result. Ten unit/negative-control tests pass:
removing an acquisition or drop reason fails, duplicate IDs fail, trace loss
fails, generations remain isolated, repeat presentation is distinct, a removed
repeat GPU completion fails, IPC replacement attributes the PREVIOUS frame, truncated captures fail, and transport sequence resets fail.

The Windows worker must collect the new end-to-end trace for actual performance
qualification. `gpu_complete` is a submission fence observation, not transport
buffer retirement: a frame may be rendered again after its earlier GPU use ends.
