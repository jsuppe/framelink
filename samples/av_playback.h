#pragma once
#include "framelink_session.h"
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
struct PlaybackOptions {
  std::string plugin, session, format = "BGRA", audioSink = "autoaudiosink";
  unsigned width=160, height=90, fps=50, rate=48000, channels=2;
  unsigned timeoutMs=15000, latencyMs=100;
  bool externalVideo=false;
  const std::atomic<bool>* mute=nullptr;
  // Live pause suspends BOTH tracks and tears down their session. Resume
  // negotiates a fresh identity/epoch; it is not lossless recorded-media pause.
  const std::atomic<bool>* paused=nullptr;
  unsigned recoveryAttempts=3;
};
// Arrival/queue telemetry is not physical output timing. queueEmptyEvents
// includes startup and EOS; it must not be labeled device underruns.
struct PlaybackMetrics {
  uint64_t audioBlocks=0, audioSamples=0, queueTimeNs=0, peakQueueTimeNs=0;
  unsigned queueBuffers=0, peakQueueBuffers=0;
  uint64_t queueEmptyEvents=0, queueFullEvents=0, lateAudioBlocks=0;
  int64_t arrivalDriftNs=0; // (last-first arrival) - (last-first media PTS)
};
struct PlaybackCallbacks {
  std::function<void(const flSessionConfig&, const std::string&, const PlaybackClock&)> onSession;
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
