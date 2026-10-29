#include "sample_common.h"
using namespace sample;
int main(int argc, char **argv) {
  Options o;
  try {
    o = parse(argc, argv, false);
  } catch (const std::exception &e) {
    fprintf(stderr, "options: %s\n", e.what());
    return 2;
  }
  if (o.help) {
    usage(false);
    return 0;
  }
  uint64_t received = 0, verified = 0, timeouts = 0, sequenceGaps = 0,
           patternGaps = 0, previousId = 0, previousSeq = 0;
  bool hadId = false;
  auto started = Clock::now();
  try {
    Channel ch;
    Pixels pixels;
    uint32_t flags = (o.map ? FL_MAP_CPU : 0) | (o.gpu ? FL_GPU_SYNC : 0) | (o.render ? FL_GPU_RENDER : 0);
    check(flCreateChannelOnAdapter(
              o.channel.c_str(), o.width, o.height, o.format, o.pool, flags,
              o.adapterSet ? o.adapter : nullptr, &ch.value),
          "create channel");
    report(ch.value);
    auto progress = Clock::now();
    double latencySum = 0, latencyMax = 0;
    uint64_t timed = 0;
    while (received < o.frames) {
      flFrame frame{};
      flResult r = flAcquireFrame(ch.value, &frame, o.timeout);
      if (r == FL_TIMEOUT || r == FL_DISCONNECTED) {
        ++timeouts;
        require(Clock::now() - progress < std::chrono::milliseconds(o.deadline),
                "frame acquisition idle deadline");
        if (r == FL_DISCONNECTED)
          std::this_thread::sleep_for(std::chrono::milliseconds(5));
        continue;
      }
      check(r, "acquire frame");
      if (o.gpu)
        pixels.waitReady(ch.value, frame);
      flChannelInfo info{};
      check(flQuery(ch.value, &info), "query");
      if (o.verify) {
        uint64_t id = pixels.verify(ch.value, frame.image, info);
        if (hadId) {
          require(id > previousId, "duplicate or backwards pattern identity");
          patternGaps += id - previousId - 1;
        }
        previousId = id;
        hadId = true;
        ++verified;
      }
      if (previousSeq && frame.sequence > previousSeq + 1)
        sequenceGaps += frame.sequence - previousSeq - 1;
      previousSeq = frame.sequence;
      if (frame.ptsNs >= 0) {
        double ms = (now() - frame.ptsNs) / 1e6;
        if (ms >= 0) {
          latencySum += ms;
          latencyMax = std::max(latencyMax, ms);
          ++timed;
        }
      }
      if (o.delay)
        std::this_thread::sleep_for(std::chrono::milliseconds(o.delay));
      if (o.gpu)
        pixels.signalConsumed(ch.value, frame);
      check(flRelease(ch.value, &frame), "release");
      ++received;
      progress = Clock::now();
    }
    auto elapsed =
        std::chrono::duration<double>(Clock::now() - started).count();
    printf("consumer received=%llu verified=%llu pattern_gaps=%llu "
           "sequence_gaps=%llu timeouts=%llu fps=%.2f pts_age_mean_ms=%.3f "
           "pts_age_max_ms=%.3f\n",
           (unsigned long long)received, (unsigned long long)verified,
           (unsigned long long)patternGaps, (unsigned long long)sequenceGaps,
           (unsigned long long)timeouts, received / elapsed,
           timed ? latencySum / timed : 0, latencyMax);
    return 0;
  } catch (const std::exception &e) {
    fprintf(stderr, "consumer FAIL after %llu frames: %s\n",
            (unsigned long long)received, e.what());
    return 1;
  }
}
