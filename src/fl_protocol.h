// framelink wire format.
//
// Owned by framelink rather than borrowed. The spike reused vkrender's
// vkr::proto because that put the already-proven transport under test, but the
// names (ClientHello, PoolDesc) describe a compositor's relationships, not a
// frame channel's, and a library cannot ship depending on an application's
// protocol.
//
// Deliberately small: a consumer owns the ring, so there is nothing to
// negotiate. The consumer states what it allocated and the producer accepts it
// or disconnects.
#pragma once
#include <stdint.h>

namespace fl {

constexpr uint32_t kMagic = 0x314B4C46; // "FLK1" little-endian
constexpr uint32_t kMaxPool = 8;

enum class MsgType : uint32_t {
    Hello = 1,      // consumer -> producer: version + adapter
    RingDesc = 2,   // consumer -> producer: the buffers, duplicated in
    FrameReady = 3, // producer -> consumer: slot N is drawn
    Bye = 4,        // either way: orderly shutdown
};

#pragma pack(push, 4)

struct MsgHeader {
    uint32_t magic;
    MsgType type;
    uint32_t payloadSize;
};

// First message on every connection, so a version mismatch is refused before
// any handle is duplicated into another process.
struct HelloMsg {
    uint32_t version;    // FL_VERSION of the sender
    uint32_t pid;
    uint32_t luidValid;  // nonzero if adapterLUID is meaningful
    uint32_t _pad;
    uint8_t  adapterLUID[8];
};

struct RingDescMsg {
    uint32_t width, height;
    uint32_t format;     // flFormat
    uint32_t poolSize;
    uint64_t texture[kMaxPool];  // NT handles, valid in the RECEIVING process
    uint64_t readyFence;         // producer signals, consumer waits
    uint64_t consumedFence;      // consumer signals, producer waits
};

struct FrameReadyMsg {
    uint32_t slot;
    uint32_t _pad;
    uint64_t readyValue; // the ready-fence value that covers this frame
    int64_t  ptsNs;      // -1 when the producer does not timestamp
};

#pragma pack(pop)

} // namespace fl
