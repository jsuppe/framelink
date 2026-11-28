#!/bin/sh
# M6.4 growth across repeated faults: 100 cycles of 5 s in one long-lived
# process, the fresh peer faulted on every odd cycle (fl_gst_av_bench
# --cycle-fault), at the primary profile on one 1080p60 cell.
#   run_cycle_faults.sh BENCH PLUGIN OUT_DIR CELL [CYCLES]
#   CELL: linux-native (NV12), android-native (RGBA), linux-shared-cpu (NV12)
# On Android, PLUGIN is ignored (static build); pass x. Gate: check_cycle_faults.py.
set -eu
bench=$1 plugin=$2 out=$3 cell=$4 cycles=${5:-100}
mkdir -p "$out"
profile="--size 1920x1080 --fps 60 --pool 2 --video-queue 1 --output-queue-buffers 1 --latency-ms 0 --transport-only --no-output-preroll --seconds 5 --cycles $cycles"
case $cell in
 linux-native) mem="--native --format NV12";;
 android-native) mem="--native --format RGBA";;
 linux-shared-cpu) mem="--shared-cpu --format NV12";;
 *) echo "unknown cell $cell"; exit 2;;
esac
run() { label=$1; shift
  "$bench" "$plugin" $profile $mem "$@" --out "$out" --label "$label" > "$out/$label.stdout" 2>&1 && rc=0 || rc=$?
  echo "$label rc=$rc $(grep -E '^(complete|cycles.ok)=' "$out/$label.summary.txt" 2>/dev/null | tr '\n' ' ')" | tee -a "$out/run.log"
}
run "$cell-restart-producer-kill" --restart producer --cycle-fault kill --cycle-fault-at-ms 3000
run "$cell-restart-consumer-kill" --restart consumer --cycle-fault kill --cycle-fault-at-ms 3000
run "$cell-restart-producer-malformed" --restart producer --cycle-fault malformed --cycle-fault-frame 180
