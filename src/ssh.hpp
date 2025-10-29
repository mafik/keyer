#pragma once

#include <libssh/libssh.h>

namespace atmt {

// SSH session
extern ssh_channel ssh_chan;
extern bool ssh_connected;

// Called on the main thread whenever WiFi establishes connection
void OnNetworkConnectedSSH();

} // namespace atmt
