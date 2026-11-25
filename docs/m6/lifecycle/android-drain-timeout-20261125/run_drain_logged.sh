#!/bin/sh
# Diagnostic run (not qualification) of the Android shared NV12 4K30
# restart-producer lifecycle cell, exactly as m6_bench_matrix.py runs it, with
# the drain handshake logged by framelinkavsrc/framelinkavsink (INFO) and the
# per-cycle files kept on the device, then pulled.
# usage: run_drain_logged.sh SERIAL BENCH OUT [RUN_NAME]
set -eu
SERIAL=$1 BENCH=$2 OUT=$3 NAME=${4:-run}
R=/data/local/tmp/framelink-m6-drain
CELL="--size 3840x2160 --fps 30 --format NV12 --mode bin --shared-cpu --pool 2 --video-queue 1 --output-queue-buffers 1 --latency-ms 0 --transport-only --no-output-preroll --cycles ${CYCLES:-100} --seconds 5 --restart producer"
mkdir -p "$OUT"
adb -s "$SERIAL" shell "mkdir -p $R/$NAME && chmod 700 $R/$NAME"
adb -s "$SERIAL" push "$BENCH" "$R/fl_gst_av_bench" >/dev/null
adb -s "$SERIAL" shell "cd $R && export LD_LIBRARY_PATH=. TMPDIR=$R/$NAME G_DEBUG=fatal-warnings GST_DEBUG=framelinkavsrc:4,framelinkavsink:4 GST_DEBUG_NO_COLOR=1 && ./fl_gst_av_bench static --out $R/$NAME --label cell $CELL; echo bench-exit=\$?" > "$OUT/$NAME.log" 2>&1
adb -s "$SERIAL" pull "$R/$NAME" "$OUT/" >/dev/null
tail -1 "$OUT/$NAME.log"
