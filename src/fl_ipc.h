// Minimal message channel for framelink's control plane.
//
// Frames never travel through here - only the handles that name them and a
// few bytes per frame to say which slot is ready. Windows uses a named pipe;
// the POSIX implementation will need fd passing (SCM_RIGHTS) for dma-buf, which
// is why recv() carries a place for descriptors even though Windows never
// fills it.
#pragma once
#include <stdint.h>

#include <optional>
#include <string>
#include <vector>

#include "fl_protocol.h"

namespace fl {

struct Message {
    MsgType type{};
    std::vector<uint8_t> payload;
    std::vector<int> fds; // POSIX only; empty on Windows

    template <typename T>
    const T* as() const {
        return payload.size() >= sizeof(T) ? reinterpret_cast<const T*>(payload.data())
                                           : nullptr;
    }
};

class Channel {
  public:
    Channel() = default;
    ~Channel();
    Channel(Channel&&) noexcept;
    Channel& operator=(Channel&&) noexcept;
    Channel(const Channel&) = delete;
    Channel& operator=(const Channel&) = delete;

    // Blocks until a peer connects. The consumer (ring owner) listens.
    static Channel listen(const std::string& name);
    // Fails fast when the name does not exist - a producer attaches to a
    // channel or it does not; it never creates one.
    static Channel connect(const std::string& name, uint32_t timeoutMs);

    bool send(MsgType type, const void* payload, size_t size);
    bool send(MsgType type) { return send(type, nullptr, 0); }
    template <typename T>
    bool send(MsgType type, const T& value) {
        return send(type, &value, sizeof(T));
    }

    std::optional<Message> recv();

    bool connected() const { return connected_; }
    uint32_t peerPid() const { return peerPid_; } // listen() side only

  private:
    void close();
    void* handle_ = nullptr; // HANDLE on Windows, int fd on POSIX
    bool connected_ = false;
    uint32_t peerPid_ = 0;
};

} // namespace fl
