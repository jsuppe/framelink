#!/bin/sh
cd ~/dev/framelink-m6
export LD_LIBRARY_PATH=EVIDENCE/bin:${LD_LIBRARY_PATH:-}
for c in linux-gpu-readback-bgra-1080p60 linux-gpu-readback-bgra-4k30 linux-native-bgra-1080p60 linux-native-bgra-4k30 linux-native-nv12-4k30 linux-native-nv12-1080p60-element; do
  echo "== $c $(date -u +%H:%M:%S)"; sh docs/m6/fault-coverage/remaining-cells/run_cells.sh EVIDENCE/bin/fl_gst_av_bench EVIDENCE/bin/libgstframelink.so EVIDENCE/linux/$c $c 5 >/dev/null
  echo "$c pass $(grep -c 'pass=yes' EVIDENCE/linux/$c/run.log)/$(wc -l < EVIDENCE/linux/$c/run.log)"; grep -v 'pass=yes' EVIDENCE/linux/$c/run.log || true
done
sh docs/m6/fault-coverage/remaining-cells/run_cycle_cells.sh EVIDENCE/bin/fl_gst_av_bench EVIDENCE/bin/libgstframelink.so EVIDENCE/linux/cycles linux-gpu-readback-bgra-1080p60
for f in EVIDENCE/linux/cycles/*.summary.txt; do python3 docs/m6/fault-coverage/cycle-faults/check_cycle_faults.py $f | tail -1; done
echo DONE
