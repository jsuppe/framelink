#!/bin/sh
# Diagnostic runs (not qualification) of one lifecycle cell, 100 cycles,
# primary profile, as recorded at e1284833, with the memory-kind sampler and
# the large-allocation shim:
#   A: as recorded.
#   B: glibc mmap threshold fixed at 8 MiB (GLIBC_TUNABLES).
# usage: run_cells.sh OUT BUILD SHIM PATH_FLAG FORMAT RESTART
#   e.g. run_cells.sh out build shim --native BGRA consumer
set -eu
OUT=$1 B=$2 SHIM=$3 PATHFLAG=$4 FMT=$5 RESTART=$6
HERE=$(dirname "$0")
CELL="--seconds 5 --fps 30 --size 3840x2160 --format $FMT --mode bin $PATHFLAG --pool 2 --video-queue 1 --output-queue-buffers 1 --latency-ms 0 --transport-only --no-output-preroll --cycles 100 --restart $RESTART"
mkdir -p "$OUT/A-log" "$OUT/B-log"
LD_PRELOAD=$SHIM BIGALLOC_LOG=$OUT/A-log/alloc python3 -I "$HERE/smaps_sampler.py" "$OUT/A" "$B/fl_gst_av_bench" "$B/libgstframelink.so" $CELL
sleep 8
GLIBC_TUNABLES=glibc.malloc.mmap_threshold=8388608 LD_PRELOAD=$SHIM BIGALLOC_LOG=$OUT/B-log/alloc python3 -I "$HERE/smaps_sampler.py" "$OUT/B" "$B/fl_gst_av_bench" "$B/libgstframelink.so" $CELL
echo done
