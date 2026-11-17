# Source-only diagnostic coverage correction

Original450s source-only run exit0/EOS measured resource use (CPU37.15%,handles10..10,RSS flat), but sourceChild did not attach push probes or count buffers. It cannot verify13500video21600000PCM or infer source timing. Those were expected counts, not observed. Preserve result as resource-only; no scheduling verdict.

Temporary diagnostic build now attaches the existing producer push probe to named source pads, counts actual video/audio payload independently, emits all raw PTS/lateness rows including startup, and rejects missing counts/timing rows. Sources restored after build; production path unchanged. Build/source hashes attached.10s preflight verifies300video480000PCM,300video and1000audio timing rows. Video max2.97ms; audio max71.06ms retained. No attribution of original transport startup failure yet. Next matched450s observed source control; avoid another uninstrumented run. Not transport qualification. Raw: <linux-home>/dev/evidence/m6-cpu-path/android-4k-stage-probe.

## Completed observed450s control

Actual13500video/21600000PCM;13500video and45000audio timing rows,EOS/exit0. Video max1.346389ms(index2424),firsttwo0.068846/1.170974ms; no34ms sourcevideo misses. Audio startup max54.569308ms with3/45000over34ms retained. Thus isolated sourcevideo did not reproduce16.907ms second-frame delay under transport; this does not prove transport alone caused it or waive audio startup failure. No additional source-only rerun warranted. BGRA4K acceptance remains suspended for a targeted producer source-to-sink blocking/allocation audit, owned Evelyn; resume with identified mechanism and matched control, no threshold changes.

Production benchmark rebuilt and hash verifiedf8a42651. Advance independent affected NV124K30 final-revision refresh:5s150video240000PCM,zero34ms video/audio misses. Does not qualify BGRA. Source/library unchanged fromd8023c4.
