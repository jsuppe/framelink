#!/bin/sh
# M6.4 malformed metadata on one producer buffer, on one platform's supported
# 1080p60 cell, primary profile. Each case declares its expected outcome
# (fl_gst_av_bench --fault*, all explicit-failure); REPEATS each.
#   run_malformed_faults.sh BENCH PLUGIN OUT_DIR CELL [REPEATS]
#   CELL: linux-native (NV12, DMA-BUF receiver), android-native (RGBA, AHB receiver),
#         linux-shared-cpu (NV12, shared-CPU ref)
# On Android, PLUGIN is ignored (static build); pass x.
set -eu
bench=$1 plugin=$2 out=$3 cell=$4 repeats=${5:-5}
mkdir -p "$out"
profile="--size 1920x1080 --fps 60 --pool 2 --video-queue 1 --output-queue-buffers 1 --latency-ms 0 --transport-only --no-output-preroll --arrival-budget-ms 17 --seconds 12 --raw"
case $cell in
 linux-native) mem="--native --format NV12";;
 android-native) mem="--native --format RGBA";;
 linux-shared-cpu) mem="--shared-cpu --format NV12";;
 *) echo "unknown cell $cell"; exit 2;;
esac
run() { label=$1; shift
  "$bench" "$plugin" $profile $mem "$@" --out "$out" --label "$label" > "$out/$label.stdout" 2>&1 && rc=0 || rc=$?
  echo "$label rc=$rc $(grep -E '^fault.pass=' "$out/$label.summary.txt" 2>/dev/null)" | tee -a "$out/run.log"
}
i=1
while [ $i -le $repeats ]; do
  run "$cell-video-no-pts-$i" --fault video-no-pts --fault-frame 360 --fault-expect explicit-failure
  run "$cell-video-stale-meta-$i" --fault video-stale-meta --fault-frame 360 --fault-expect explicit-failure
  run "$cell-audio-no-pts-$i" --fault audio-no-pts --fault-frame 600 --fault-expect explicit-failure
  run "$cell-audio-misaligned-$i" --fault audio-misaligned --fault-frame 600 --fault-expect explicit-failure
  i=$((i+1))
done
