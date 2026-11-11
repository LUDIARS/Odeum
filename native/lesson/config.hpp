#pragma once
#include "web_access.hpp"
#include <cstdint>
#include <string>

namespace odeum::lesson {
struct Config {
    std::string bind = "127.0.0.1";
    std::uint16_t port = 4410;           // students: /lesson, /v1/join
    std::uint16_t console_port = 4411;   // operator: always bound to 127.0.0.1
    std::size_t max_students = 60;       // per class
    std::string console_token_file;      // written at startup, owner-readable only
    std::string plot_file;               // optional extra course
    bool browser_stt = false;            // let student browsers transcribe speech (audio leaves the device; see spec)
    relay::WebAccess web_access;
};
// Reads ODEUM_LESSON_* once. Set-but-empty and out-of-range values throw; nothing falls back silently.
Config load_config();
}
