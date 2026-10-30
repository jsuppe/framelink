#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
namespace av {
struct Endpoint { uint16_t port = 0; std::array<unsigned char, 32> token{}; };
// Local current-user trust boundary. Keep owner alive while listening. An OS
// lifetime lock allows recovery after process death without trusting PID files.
class RendezvousOwner {
public:
    RendezvousOwner();
    ~RendezvousOwner();
    RendezvousOwner(const RendezvousOwner&) = delete;
    RendezvousOwner& operator=(const RendezvousOwner&) = delete;
    bool publish(const std::string& directory, const Endpoint&, std::string& error);
    void close();
private:
    struct Impl;
    Impl* impl_ = nullptr;
};
bool readEndpoint(const std::string& directory, Endpoint&, std::string& error);
bool secureRandom(void*, size_t, std::string& error);
}
