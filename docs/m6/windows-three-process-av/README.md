# Three-process local authenticated A/V relay

Implemented against486bf04 with exact changed-source hashes recorded here. Windows focused build and4/4 checks pass (3.43s): new three-process A/V, prior video-only graph, source control, prior FAR5 process control. Independent review found no actionable issues.

Source child -> relay parent -> final-reader child, with authenticated FAR4 input/FAR5 same-allocation output. Only control events are inherited, never textures. Consumer snapshot pins the original pool for the entire graph. SessionNativeRelay/SessionPcmRelay forward real native leases and authenticated PCM. Independent final-reader oracle checks8 native frame pixels/PTS/DTS/duration/generation,8 PCM blocks/384 stereo frames/1536 exact bytes, common mapped37ms A/V offset, authenticated video END and audio EOS. Final native lease remains held until explicit reader release; collection cannot retire it early. Source control acknowledgment follows actual final-reader release.

No production defaults/API/wire changes. This is small deterministic functional coverage, not throughput, repeated reconnect, resource acceptance or physical presentation. Default device-lifetime and timing failures remain open. Next: fresh-generation reconnect/old-held-graph isolation controls for this composed path. M6 open; M7 held.
