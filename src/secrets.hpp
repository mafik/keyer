#pragma once

// Secret snippets that can be entered by some chords
extern const wchar_t *SECRET_SNIPPET[4];

// Hardcoded WiFi SSID
extern const char *WIFI_SSID;

// Hardcoded WiFi password
extern const char *WIFI_PASSWORD;

// Hardcoded SSH host
extern const char *SSH_HOST;

// Hardcoded SSH port
extern const int SSH_PORT;

// Hardcoded SSH user
extern const char *SSH_USER;

// Hardcoded SSH private key (one of the "--BEGIN OPENSSH PRIVATE KEY..." type)
extern const char *SSH_PRIVATE_KEY;

// NOTE: SSH_PROXYJUMP is not really supported on ESP32 due to lack of several
// POSIX APIs...
