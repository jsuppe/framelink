cd .
D=$OUT/ab-rss
P="--gpu-readback --format BGRA --pool 2 --video-queue 1 --output-queue-buffers 1 --latency-ms 0 --transport-only --no-output-preroll --raw --seconds 600 --fps 60 --size 1920x1080 --arrival-budget-ms 17"
sha256sum build-gst/fl_gst_av_bench build-gst/libgstframelink.so $PRE/build/libgstframelink.so > $D/binaries.sha256
for i in 1 2; do
 build-gst/fl_gst_av_bench $PRE/build/libgstframelink.so $P --out $D --label pre-$i > $D/pre-$i.stdout 2>&1; echo pre-$i $(grep '^complete=' $D/pre-$i.summary.txt)
 build-gst/fl_gst_av_bench build-gst/libgstframelink.so $P --out $D --label post-$i > $D/post-$i.stdout 2>&1; echo post-$i $(grep '^complete=' $D/post-$i.summary.txt)
done
MALLOC_ARENA_MAX=1 build-gst/fl_gst_av_bench build-gst/libgstframelink.so $P --out $D --label post-arena1 > $D/post-arena1.stdout 2>&1; echo post-arena1 $(grep "^complete=" $D/post-arena1.summary.txt)
echo DONE
