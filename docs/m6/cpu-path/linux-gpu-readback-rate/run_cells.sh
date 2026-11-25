#!/bin/sh
# M6.8c on Linux: the GPU-staged readback receiver's 1800 s cells, one after
# the other, with the primary zero-delay profile (as the Android CPU receiver
# cells in ../android-cpu-receiver-20261118).
#   run_cells.sh BUILD_DIR OUT_DIR [SECONDS]
set -eu
build=$1 out=$2 seconds=${3:-1800}
mkdir -p "$out"
bench="$build/fl_gst_av_bench" plugin="$build/libgstframelink.so"
sha256sum "$bench" "$plugin" > "$out/binaries.sha256"
nvidia-smi --query-gpu=name,driver_version --format=csv,noheader > "$out/gpu.txt" 2>&1 || true
profile="--gpu-readback --format BGRA --pool 2 --video-queue 1 --output-queue-buffers 1 --latency-ms 0 --transport-only --no-output-preroll --raw"
cell() { label=$1; shift
  echo "$(date -u +%FT%TZ) start $label" >> "$out/run.log"
  "$bench" "$plugin" $profile --seconds "$seconds" "$@" --out "$out" --label "$label" > "$out/$label.stdout" 2>&1 || true
  echo "$(date -u +%FT%TZ) end $label: $(grep '^complete=' "$out/$label.summary.txt" 2>/dev/null)" >> "$out/run.log"
}
cell "bgra-4k30-gpu-readback-$seconds" --fps 30 --size 3840x2160 --arrival-budget-ms 34
cell "bgra-1080p60-gpu-readback-$seconds" --fps 60 --size 1920x1080 --arrival-budget-ms 17
