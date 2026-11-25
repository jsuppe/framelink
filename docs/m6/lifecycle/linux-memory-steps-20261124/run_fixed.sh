#!/bin/sh
# Diagnostic run (not qualification) of one lifecycle cell, 100 cycles,
# primary profile, as run_cells.sh's A run but with the fixed build:
#   C: no GLIBC_TUNABLES. framelinkavsrc's CPU output pool maps each buffer,
#      and the bench's producer-side roles fix glibc's mmap threshold.
# usage: run_fixed.sh OUT BUILD SHIM PATH_FLAG FORMAT RESTART
set -eu
OUT=$1 B=$2 SHIM=$3 PATHFLAG=$4 FMT=$5 RESTART=$6
HERE=$(dirname "$0")
CELL="--seconds 5 --fps 30 --size 3840x2160 --format $FMT --mode bin $PATHFLAG --pool 2 --video-queue 1 --output-queue-buffers 1 --latency-ms 0 --transport-only --no-output-preroll --cycles 100 --restart $RESTART"
mkdir -p "$OUT/C-log"
LD_PRELOAD=$SHIM BIGALLOC_LOG=$OUT/C-log/alloc python3 -I "$HERE/smaps_sampler.py" "$OUT/C" "$B/fl_gst_av_bench" "$B/libgstframelink.so" $CELL
echo done
