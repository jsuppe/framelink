#!/bin/sh
# M6.4 decision 15: the full declared fault case set on one remaining cell,
# primary zero-delay profile, REPEATS each (fl_gst_av_bench --fault*).
#   run_cells.sh BENCH PLUGIN OUT_DIR CELL [REPEATS]
#   CELL: PLATFORM-MEMORY-FORMAT-SIZE[-element], e.g. linux-native-bgra-4k30,
#         linux-gpu-readback-bgra-1080p60, android-cpu-rgba-4k30,
#         linux-native-nv12-1080p60-element
#   MEMORY: native (zero-copy receiver), gpu-readback (Linux GPU-staged CPU
#   receiver), cpu (Android CPU receiver), shared-cpu (Windows CPU cells).
# On Android, PLUGIN is ignored (static build); pass x.
# Declared outcomes, from the documented overload policy and the receiver:
#  - native receivers hold the slot while downstream holds the buffer: a 200 ms
#    consumer stall is a counted drop at the producer (and an explicit failure
#    under error-on-ring-full); a 200 ms burst is absorbed.
#  - copy-out receivers (gpu-readback, cpu) free the slot as they copy, so a
#    200 ms stall or burst overwrites the one-frame unread queue: the consumer's
#    replacement failure (pilot: docs/m6/fault-coverage/remaining-cells/pilot).
#  - shared CPU delivers late instead: a 200 ms stall or burst is absorbed
#    (the Linux shared-CPU reference records).
set -eu
bench=$1 plugin=$2 out=$3 cell=$4 repeats=${5:-5}
mkdir -p "$out"
IFS=- read -r platform memory rest <<E
$cell
E
case $memory in gpu) memory=gpu-readback; rest=${rest#readback-};; shared) memory=shared-cpu; rest=${rest#cpu-};; esac
format=$(echo "$rest" | cut -d- -f1); size=$(echo "$rest" | cut -d- -f2); form=$(echo "$rest" | cut -d- -f3)
case $size in
 1080p60) geom="--size 1920x1080 --fps 60 --arrival-budget-ms 17"; vframe=360;;
 4k30) geom="--size 3840x2160 --fps 30 --arrival-budget-ms 34"; vframe=180;;
 *) echo "unknown size $size"; exit 2;;
esac
fmt=$(echo "$format" | tr a-z A-Z)
case $memory in
 native) mem="--native --format $fmt"; stall200=counted-drop; burst200=absorbed; strict=yes;;
 gpu-readback) mem="--gpu-readback --format $fmt"; stall200=replacement-failure; burst200=replacement-failure; strict=no;;
 cpu) mem="--format $fmt"; stall200=replacement-failure; burst200=replacement-failure; strict=no;;
 shared-cpu) mem="--shared-cpu --format $fmt"; stall200=absorbed; burst200=absorbed; strict=no;;
 *) echo "unknown memory $memory"; exit 2;;
esac
case $form in "") ;; element) mem="$mem --mode element";; *) echo "unknown form $form"; exit 2;; esac
[ -n "${STALL200:-}" ] && stall200=$STALL200
[ -n "${BURST200:-}" ] && burst200=$BURST200
profile="$geom --pool 2 --video-queue 1 --output-queue-buffers 1 --latency-ms 0 --transport-only --no-output-preroll --seconds 12 --raw"
run() { label=$1; shift
  "$bench" "$plugin" $profile $mem "$@" --out "$out" --label "$label" > "$out/$label.stdout" 2>&1 && rc=0 || rc=$?
  echo "$label rc=$rc $(grep -E '^fault.pass=' "$out/$label.summary.txt" 2>/dev/null)" | tee -a "$out/run.log"
}
i=1
while [ $i -le $repeats ]; do
  run "$cell-producer-kill-$i" --fault producer-kill --fault-at-s 6
  run "$cell-consumer-kill-$i" --fault consumer-kill --fault-at-s 6
  run "$cell-stall5-$i" --fault consumer-stall --fault-frame $vframe --fault-ms 5 --fault-expect absorbed
  run "$cell-stall200-$i" --fault consumer-stall --fault-frame $vframe --fault-ms 200 --fault-expect $stall200
  [ $strict = yes ] && run "$cell-stall200-strict-$i" --fault consumer-stall --fault-frame $vframe --fault-ms 200 --fault-expect explicit-failure --error-on-ring-full
  run "$cell-burst5-$i" --burst-queue --fault producer-burst --fault-frame $vframe --fault-ms 5 --fault-expect absorbed
  run "$cell-burst200-$i" --burst-queue --fault producer-burst --fault-frame $vframe --fault-ms 200 --fault-expect $burst200
  run "$cell-astall50-$i" --burst-queue --fault audio-stall --fault-frame 600 --fault-ms 50 --fault-expect absorbed
  run "$cell-astall200-$i" --burst-queue --fault audio-stall --fault-frame 600 --fault-ms 200 --fault-expect absorbed
  run "$cell-adrop50-$i" --fault audio-drop --fault-frame 600 --fault-ms 50 --fault-expect explicit-failure
  run "$cell-agap50-$i" --fault audio-gap --fault-frame 600 --fault-ms 50 --fault-expect gap-visible
  run "$cell-video-no-pts-$i" --fault video-no-pts --fault-frame $vframe --fault-expect explicit-failure
  run "$cell-video-stale-meta-$i" --fault video-stale-meta --fault-frame $vframe --fault-expect explicit-failure
  run "$cell-audio-no-pts-$i" --fault audio-no-pts --fault-frame 600 --fault-expect explicit-failure
  run "$cell-audio-misaligned-$i" --fault audio-misaligned --fault-frame 600 --fault-expect explicit-failure
  i=$((i+1))
done
