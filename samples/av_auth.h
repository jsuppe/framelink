#pragma once
#include "av_rendezvous.h"
#include <gio/gio.h>
namespace av {
// Mutually authenticate a bounded role-specific GIO stream using the private
// rendezvous key. No secret bytes are sent. Caller sets finite socket timeouts.
bool authenticateClient(GSocketConnection*, const Endpoint&, char role, std::string& error);
bool authenticateServer(GSocketConnection*, const Endpoint&, char expectedRole, std::string& error);
}
