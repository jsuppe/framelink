#!/bin/sh
cd .
echo "rate $(date -u +%H:%M)"; sh docs/m6/cpu-path/linux-gpu-readback-rate/run_cells.sh build-gst $OUT/rate 1800; cat $OUT/rate/run.log
for c in linux-gpu-readback-bgra-1080p60 linux-gpu-readback-bgra-4k30; do
  echo "== $c $(date -u +%H:%M)"; sh docs/m6/fault-coverage/remaining-cells/run_cells.sh build-gst/fl_gst_av_bench build-gst/libgstframelink.so $OUT/cells/$c $c 5 >/dev/null
  echo "$c pass $(grep -c 'pass=yes' $OUT/cells/$c/run.log)/$(wc -l < $OUT/cells/$c/run.log)"; grep -v 'pass=yes' $OUT/cells/$c/run.log || true
done
sh docs/m6/fault-coverage/remaining-cells/run_cycle_cells.sh build-gst/fl_gst_av_bench build-gst/libgstframelink.so $OUT/cycles linux-gpu-readback-bgra-1080p60
for f in $OUT/cycles/*.summary.txt; do python3 docs/m6/fault-coverage/cycle-faults/check_cycle_faults.py $f | tail -1; grep '^cycle.slot_drops' $f; done
echo DONE
