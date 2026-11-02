#pragma once
#include "framelink_session.h"
#include "av_session_provider.h"
#include <gst/gst.h>
#include <atomic>
#include <functional>
#include <string>
namespace av {
// Callbacks run on receiver thread. Borrowed clock/sample: ref before retaining.
// Native mode gives exclusive video consumer ownership to caller. Image readiness
// fences are separate from presentation deadline = base + running PTS + latency.
struct PlaybackClock {
  GstClock *clock = nullptr;
  uint64_t baseTimeNs = 0, latencyNs = 0;
  bool audioMaster = false;
};
enum class PlaybackClockPolicy { AudioPreferred, ProducerMapped, AudioPreferredCoordinated };
struct PlaybackOptions {
  std::string plugin, session, format = "BGRA", audioSink = "autoaudiosink";
  unsigned width=160, height=90, fps=50, rate=48000, channels=2;
  unsigned timeoutMs=15000, latencyMs=100;
  // Opt-in modes 3/5: finite media timeline budget (1..300000 ms); zero keeps
  // legacy whole-session timeout. Requires matching producer and a coordinated policy.
  // Startup/idle bounds stay unchanged; active deadline = budget + timeoutMs.
  unsigned mediaRunMs=0;
  bool externalVideo=false;
  const std::atomic<bool>* mute=nullptr;
  // Live pause suspends BOTH tracks and tears down their session. Resume
  // negotiates a fresh identity/epoch; it is not lossless recorded-media pause.
  const std::atomic<bool>* paused=nullptr;
  unsigned recoveryAttempts=3;
  // Optional trusted broker adapter. Both callbacks are required.
  const SessionProvider* sessionProvider=nullptr;
  // ProducerMapped primes audio, then negotiates a final shared epoch on the
  // receiver system clock and slaves the audio device. Requires mode-2 peer
  // support (older producers reject explicitly); avoids startup catch-up bursts.
  // onSession config epoch is provisional; onClock carries the final mapping.
  // AudioPreferredCoordinated uses the same bounded PRIMED/GO barrier (mode 4,
  // or 5 with finite budget), then anchors BOTH tracks on the device clock.
  // No media PTS is rewritten; system-clock fallback is explicit when the sink
  // cannot provide a clock. Legacy AudioPreferred/mode 1 is unchanged.
  PlaybackClockPolicy clockPolicy=PlaybackClockPolicy::AudioPreferred;
};
// One broker-owned producer session. The provider supplies server ends of two
// distinct connected byte streams. Broker must shutdown them on revoke/stop.
// Optional SessionProvider::openVideo transfers the native channel to the
// pipeline; without it, video keeps the existing named transport. Cross-UID
// callers must supply authorized video ownership as well as C/A streams.
struct ProducerOptions {
  std::string plugin, format="BGRA";
  unsigned frames=150, width=160, height=90, fps=50, rate=48000, channels=2;
  // Same bounds as the CLI: frames 1..100000, size 2..4096 x 2..2160,
  // fps 13..240, timeout 100..60000 ms. Default startup remains 15 seconds.
  unsigned timeoutMs=15000;
  // Zero keeps whole-session timeout; otherwise exact finite peer budget,
  // 1..300000 ms, covering frames/fps without widening startup/idle limits.
  unsigned mediaRunMs=0;
  // Optional cooperative cancellation. Provider callbacks must still honor
  // timeoutMs; native video setup/teardown may take that bounded interval.
  const std::atomic<bool>* stop=nullptr;
  const SessionProvider* sessionProvider=nullptr;
  // Optional source-content adapter. Called on the producer thread after setup;
  // borrowed sink, returns one OWNED buffer or nullptr/throws to fail the session.
  // Must preserve the supplied PTS/duration and obey negotiated native readiness.
  // The provider configures matching caps/allocation policy before startup. No
  // implicit map, copy or fallback is introduced by this callback.
  std::function<GstBuffer*(GstElement*,unsigned,uint64_t,uint64_t)> videoFrame;
};
// Returns false on failure/cancel/deadline; never terminates the host process.
bool runBrokerProducer(const ProducerOptions&, std::string& error);
// Arrival/queue telemetry is not physical output timing. queueEmptyEvents
// includes startup and EOS; it must not be labeled device underruns.
struct PlaybackMetrics {
  uint64_t audioBlocks=0, audioSamples=0, queueTimeNs=0, peakQueueTimeNs=0;
  unsigned queueBuffers=0, peakQueueBuffers=0;
  uint64_t queueEmptyEvents=0, queueFullEvents=0, lateAudioBlocks=0;
  int64_t arrivalDriftNs=0; // decoded PCM admission: arrival delta minus media PTS delta
};
struct PlaybackCallbacks {
  // Optional decoded-PCM observer, on the audio streaming thread (unlike the
  // receiver-thread callbacks below). Borrowed buffer, original running-time
  // PTS, before resampling. Must be bounded/thread-safe; never rewrite media.
  // An exception invalidates the session rather than crossing the C callback.
  std::function<void(GstBuffer*, uint64_t)> onAudio;
  std::function<void(const flSessionConfig&, const std::string&, const PlaybackClock&)> onSession;
  // Optional native resource preparation after transport/audio preroll, before
  // PRIMED and final producer epoch in either coordinated policy (legacy mode has no
  // producer gate). Must finish within remaining startup budget;
  // may throw to fail setup. Must not read unproduced image pixels.
  std::function<void(unsigned remainingTimeoutMs)> onPrepare;
  // onSession clock is null during setup. onClock enables presentation after
  // audio preroll; it supplies the single shared epoch for both tracks.
  std::function<void(const PlaybackClock&)> onClock;
  std::function<void(GstSample*, uint64_t, const PlaybackClock&)> onVideo;
  std::function<void(const std::string&)> onStatus;
  std::function<void()> onEnd;
  std::function<void(const PlaybackMetrics&)> onMetrics;
};
// Returns false with error on cancellation, EOS before completion, or failure.
// Audio is rendered by one pipeline; no per-frame media acknowledgement.
// Stop is polled during playback; connection operations have bounded deadlines.
bool runPlayback(const PlaybackOptions&, const PlaybackCallbacks&,
                 const std::atomic<bool>& stop, std::string& error);
}
