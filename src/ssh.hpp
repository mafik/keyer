#pragma once

#include <libssh/libssh.h>

namespace atmt {

// When non-nullptr, HandleUnicode/HandleKey route keyboard input to the SSH
// channel instead of BLE.
extern ssh_channel ssh_chan;

// Start an SSH session on a background task. Sets ssh_active=true.
// Keyboard input is routed to the SSH channel by HandleUnicode/HandleKey.
// SSH output is typed via BLE keyboard.
// When the session ends, ssh_active is cleared.
void StartSSHSession();

} // namespace atmt
