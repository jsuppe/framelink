# Windows simultaneous A/V sustained results

RTX5090/Caspar, GStreamer1.28, native BGRA/NV12 1080p60, two original allocations,
independent video and PCM pacing,3720 frames/120warmup. All four trace-enabled
one/two-hop cases retained3720/3720 frames and2976000 stereo PCM sample-frames.
Exact37ms relative media PTS preserved; arrival/transport skew reported separately.

NV12 one/two:60.000111/59.999299fps. BGRA one/two:59.998325/59.996728fps.
All GPU completions accounted;3720/7440 boundary joins;zero trace loss.
Nominal traced/untraced video overhead matrix is separately in ../windows/.

Test-only GDP/TCP PCM, not authenticated production-session qualification. Source
CPU fill/upload explicit. Consumer D3D11 CopyResource to private staging + readback
is counted:3110400 NV12 or8294400 BGRA bytes/frame. No relay map/payload copy.
This qualifies native boundary ownership/GPU copy completion, not shader rendering,
physical scanout, acoustic sync, or end-to-end zero-copy decoder integration.

EXE SHA2561680C61D248CB00E00A54A419AA57C3B04FE5F99E256D82604A13DF811366704;
DLL2F07503EEF44BBEE1662D95EA9482540ABDEAAF9A6C0E876040325C1DF6E1D8E.
Commands: FL_BENCH_AUDIO=1 FL_BENCH_FORMAT=BGRA orNV12 FL_BENCH_HOPS=1 or2,
FL_BENCH_OUT=prefix FRAMELINK_TRACE=prefix-trace FL_BENCH_TRACE=prefix-client,
both *_EVENTS=1048576. Ensure GStreamer bin on PATH. Run benchmark EXE.
Use check_boundary_av, check_boundary_benchmark and check_boundary_trace;
trace checker uses --producer-map-operation flImageMapPlanes, --completion-files
client CSVs and --private-readback-bytes with the format size above.

Resource/flush accounting and final milestone gates remain separate.
