// Local A/V session control contract. Audio remains separate from flFrame.
#pragma once
#include "framelink.h"
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
#define FL_SESSION_VERSION 1u
#define FL_SESSION_MAX_FORMATS 8u
#define FL_SESSION_WIRE_SIZE 160u

typedef struct flSessionId { uint8_t bytes[16]; } flSessionId;
typedef enum flPcmFormat { FL_PCM_S16 = 1, FL_PCM_F32 = 2 } flPcmFormat;
typedef enum flChannelLayout { FL_LAYOUT_MONO = 1, FL_LAYOUT_STEREO = 2 } flChannelLayout;
typedef enum flSessionResult {
    FL_SESSION_OK = 0, FL_SESSION_INVALID, FL_SESSION_VERSION_ERROR,
    FL_SESSION_INCOMPATIBLE, FL_SESSION_STALE, FL_SESSION_TIME_RANGE
} flSessionResult;
typedef struct flAudioFormat { uint32_t pcm, rate, layout; } flAudioFormat;
typedef struct flVideoFormat { uint32_t format, width, height, fpsNum, fpsDen; } flVideoFormat;
typedef struct flSessionCapabilities {
    uint32_t version, videoCount, audioCount;
    flVideoFormat video[FL_SESSION_MAX_FORMATS];
    flAudioFormat audio[FL_SESSION_MAX_FORMATS];
} flSessionCapabilities;
// All times are nanoseconds. PTS is nonnegative relative to session epoch.
// Origin clockId identifies a particular running clock instance, NOT a clock type.
typedef struct flSessionConfig {
    uint32_t version;
    flSessionId sessionId, clockId;
    uint64_t generation;
    uint32_t videoTrackId, audioTrackId;
    flVideoFormat video;
    flAudioFormat audio;
    int64_t epochNs;
    uint32_t videoQueueFrames, audioQueueMs, connectTimeoutMs, idleTimeoutMs;
} flSessionConfig;
typedef struct flClockMapping {
    flSessionId sourceClockId, targetClockId;
    int64_t sourceAnchorNs, targetAnchorNs;
    uint64_t uncertaintyNs;
} flClockMapping;
// Validates every entry (not only the selected entry). Selection: producer video
// order; audio 48 kHz first, then producer order. Exact intersection; no conversion.
FL_API flSessionResult flSessionNegotiate(const flSessionCapabilities* producer,
    const flSessionCapabilities* consumer, flVideoFormat* video, flAudioFormat* audio);
FL_API flSessionResult flSessionValidate(const flSessionConfig* config);
// Validate a decoded selection against BOTH actual offers (syntax alone is not
// agreement). Accepts any exact common format; does not enforce preference order.
FL_API flSessionResult flSessionValidateSelection(const flSessionConfig* config,
    const flSessionCapabilities* producer, const flSessionCapabilities* consumer);
// Reject changed session/track identities and non-increasing generation on replacement.
FL_API flSessionResult flSessionValidateReplacement(const flSessionConfig* current,
    const flSessionConfig* next);
// Exactly FL_SESSION_WIRE_SIZE bytes; LE integers, magic/version, zero reserved.
FL_API flSessionResult flSessionEncode(const flSessionConfig*, void*, size_t);
FL_API flSessionResult flSessionDecode(const void*, size_t, flSessionConfig*);
// Mapping is explicitly negotiated by the application; helper does not measure it.
// Excess uncertainty is rejected. Never extrapolates rate/drift correction.
FL_API flSessionResult flSessionMapPts(const flSessionConfig*, const flClockMapping*,
    uint64_t maxUncertaintyNs, int64_t ptsNs, int64_t* targetNs);
// Timestamp offset of a sample index: floor(index * 1e9 / rate), checked for overflow.
// Block duration must be offset(first+count)-offset(first), not repeated rounding.
FL_API flSessionResult flSessionSampleOffset(uint64_t sampleIndex, uint32_t rate,
    int64_t* offsetNs);
#ifdef __cplusplus
}
#endif
