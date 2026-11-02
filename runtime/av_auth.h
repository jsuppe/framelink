#pragma once
#include "av_rendezvous.h"
#include <gio/gio.h>
namespace av {
// Mutually authenticate a bounded role-specific GIO stream using the private
// rendezvous key. No secret bytes are sent. Caller sets finite socket timeouts.
bool authenticateClient(GSocketConnection*, const Endpoint&, char role, std::string& error);
bool authenticateClientCancellable(GSocketConnection*, const Endpoint&, char role,
                                   std::string& error, GCancellable*);
bool authenticateServer(GSocketConnection*, const Endpoint&, char expectedRole, std::string& error);
// Embedded producer variant; preserves the existing desktop signature/symbol.
bool authenticateServerCancellable(GSocketConnection*, const Endpoint&, char expectedRole,
                                   std::string& error, GCancellable*);
}
