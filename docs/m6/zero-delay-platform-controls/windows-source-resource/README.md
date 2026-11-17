# Source-only Windows resource baseline

Same b5236a4 + gst/test3e0cd85 binaries; 1800s NV12 4K30 and 48kHz audio, direct to fakesinks without FrameLink transport or consumer. EOS complete, exit0. Private commit slope7.36KB/min (limit64), flat handle/thread slopes, CPU5.12%. This excludes neither consumer instrumentation nor D3D11 allocation cost. It only shows the shared source/sampler workload does not reproduce the elevated slope.

Next matched local-consumer control retains the normal queues, appsinks, callbacks, record-vector prefaulting and resource sampling but replaces FrameLink sources with live test sources. It must deliver54000video/86400000PCM and complete queue accounting; zero-copy, transport and native ownership qualification are explicitly out of scope. Private commit <=64KB/min remains the unchanged diagnostic threshold.
