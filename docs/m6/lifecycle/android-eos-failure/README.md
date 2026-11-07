# Android lifecycle EOS race

Original100x5s producer-restart run: consumer100/100 EOS with300video/240000PCM each, stable handles/threads and0.36KB/cycle RSS. Driver rejects99/100 because fresh producer cycle8 aborted before result publication. Crash log identifies SIGABRT/std::bad_alloc on vsrc:src. Exact retained binary SHA b1cedab4985eb88cd31913a0cd6d605b9485f39011762f2613577400b8f45422; symbolized PC0x311bd0 is vector<GstMessage*>::push_back in gstframelinkavsink.cpp handleMessage. Audio/video streaming threads could mutate heldEos and publish drainer without synchronization; drainer also swapped the same vector without synchronization.

Fix serializes EOS collection/drainer publication and drainer extraction under the session mutex, forwards messages outside the mutex, and accepts at most one EOS per configured child. Cancelled sessions discard late EOS. Existing teardown stops child streaming before reset/join. No device cache or resource-threshold change.

Regression uses a two-thread EOS-event barrier before child sinks so both tracks reach EOS together, requiring exact consumer counts and successful drain. Twelve focused iterations plus the normal producer-bin suite precede Android lifecycle rerun. Original failure remains archived, not relabeled as a resource pass. The remaining consumer-restart100 case was not run by the failed controller.
