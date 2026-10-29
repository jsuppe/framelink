#include "sample_common.h"
using namespace sample;
int main(int argc, char **argv) {
  Options o;
  try {
    o = parse(argc, argv, true);
  } catch (const std::exception &e) {
    fprintf(stderr, "options: %s\n", e.what());
    return 2;
  }
  if (o.help) {
    usage(true);
    return 0;
  }
  uint64_t sent = 0, timeouts = 0, reconnects = 0;
  auto started = Clock::now();
  try {
    Channel ch;
    Pixels pixels;
    auto connect = [&]() {
      auto until = Clock::now() + std::chrono::milliseconds(o.deadline);
      for (;;) {
        uint32_t slot = 0;
        flResult r = o.slots
                         ? flOpenProducerSlot(o.channel.c_str(), o.slots, 0, 0,
                                              o.format, 0, &ch.value, &slot)
                         : flOpenProducer(o.channel.c_str(), &ch.value);
        if (r == FL_OK) {
          if (o.slots)
            fprintf(stderr, "selected_slot=%u\n", slot);
          break;
        }
        require(r == FL_NOT_FOUND || r == FL_BUSY || r == FL_DISCONNECTED,
                std::string("connect: ") + flResultString(r));
        require(Clock::now() < until,
                std::string("connect deadline: ") + flResultString(r));
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
      }
      flChannelInfo info{};
      check(flQuery(ch.value, &info), "query");
      if (o.formatSet)
        require(info.format == o.format,
                "channel format differs from --format (no conversion)");
      if (o.sizeSet) {
        check(flRequestGeometry(ch.value, o.width, o.height, info.format),
              "request geometry");
        check(flQuery(ch.value, &info), "query after geometry");
        require(info.width == o.width && info.height == o.height,
                "geometry request declined");
      }
      report(ch.value, stderr);
    };
    connect();
    auto progress = Clock::now(), next = progress;
    for (uint64_t id = 0; id < o.frames;) {
      if (o.reconnect && id == o.reconnect && reconnects == 0) {
        ch.close();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        connect();
        ++reconnects;
        progress = next = Clock::now();
      }
      flBuffer buffer{};
      flResult r = flAcquireBuffer(ch.value, &buffer, o.timeout);
      if (r == FL_DISCONNECTED && sent)
        break;
      if (r == FL_TIMEOUT) {
        ++timeouts;
        require(Clock::now() - progress < std::chrono::milliseconds(o.deadline),
                "buffer acquisition idle deadline");
        continue;
      }
      check(r, "acquire buffer");
      flChannelInfo info{};
      check(flQuery(ch.value, &info), "query");
      pixels.write(ch.value, buffer.image, info, id, id == o.corrupt);
      // Disconnect after the consumer's requested capture is normal, not
      // corruption.
      r = pixels.submit(ch.value, buffer, now(), o.manual);
      if (r == FL_DISCONNECTED && sent)
        break;
      check(r, "submit");
      ++id;
      ++sent;
      progress = Clock::now();
      if (o.fps) {
        next += std::chrono::nanoseconds(1000000000 / o.fps);
        if (next < progress)
          next = progress;
        std::this_thread::sleep_until(next);
      }
    }
    fprintf(
        stderr,
        "producer sent=%llu timeouts=%llu reconnects=%llu elapsed_ms=%lld\n",
        (unsigned long long)sent, (unsigned long long)timeouts,
        (unsigned long long)reconnects,
        (long long)std::chrono::duration_cast<std::chrono::milliseconds>(
            Clock::now() - started)
            .count());
    return sent ? 0 : 1;
  } catch (const std::exception &e) {
    fprintf(stderr, "producer FAIL after %llu frames: %s\n",
            (unsigned long long)sent, e.what());
    return 1;
  }
}
