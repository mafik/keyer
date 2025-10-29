#include "secrets.hpp"

const char *WIFI_SSID = "hotspot_name";
const char *WIFI_PASSWORD = "1234qwer";
const char *SSH_HOST = "192.168.0.99";
const int SSH_PORT = 20;
const char *SSH_USER = "root";
const char *SSH_PRIVATE_KEY = R"(-----BEGIN OPENSSH PRIVATE KEY-----
b3BlbnNzaC1rZXktdjEAAAAABG5vbmUAAAAEbm9uZQAAAAAAAAABAAAAMwAAAAtzc2gtZW
QyNTUxOQAAACAug6bEu9R35P5q83waHD8VffRqDOfvi+btX2pYOy1C4AAAAJj/luHY/5bh
2AAAAAtzc2gtZWQyNTUxOQAAACAug6bEu9R35P5q83waHD8VffRqDOfvi+btX2pYOy1C4A
AAAEDAwoXdqE4Dbhmhs6I2zBSYgZtykRRarYvuc36AJx085i6DpsS71Hfk/mrzfBocPxV9
9GoM5++L5u1falg7LULgAAAAFGdlbmVyYXRlZC0xNzYxNjYxNzkzAQ==
-----END OPENSSH PRIVATE KEY-----
)";
