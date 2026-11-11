#include "config.hpp"
#include <boost/asio/ip/address.hpp>
#include <charconv>
#include <cstdlib>
#include <stdexcept>

namespace odeum::lesson {
namespace {
std::string env(const char* name, const char* fallback = "") {
    if (const auto value = std::getenv(name)) {
        if (!*value) throw std::runtime_error(std::string(name) + " cannot be empty");
        return value;
    }
    return fallback;
}
std::size_t integer(const char* name, const char* fallback, std::size_t maximum) {
    const auto value = env(name, fallback);
    std::size_t n = 0;
    const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), n);
    if (error != std::errc{} || end != value.data() + value.size() || n == 0 || n > maximum) throw std::runtime_error(std::string(name) + " is out of range");
    return n;
}
}
Config load_config() {
    Config config;
    config.bind = env("ODEUM_LESSON_BIND", "127.0.0.1");
    boost::asio::ip::make_address(config.bind);
    config.port = static_cast<std::uint16_t>(integer("ODEUM_LESSON_PORT", "4410", 65535));
    config.console_port = static_cast<std::uint16_t>(integer("ODEUM_LESSON_CONSOLE_PORT", "4411", 65535));
    if (config.port == config.console_port) throw std::runtime_error("ODEUM_LESSON_CONSOLE_PORT must differ from ODEUM_LESSON_PORT");
    config.max_students = integer("ODEUM_LESSON_MAX_STUDENTS", "60", 300);
    config.console_token_file = env("ODEUM_LESSON_CONSOLE_TOKEN_FILE");
    if (config.console_token_file.empty()) throw std::runtime_error("ODEUM_LESSON_CONSOLE_TOKEN_FILE is required");
    config.plot_file = env("ODEUM_LESSON_PLOT");
    const auto stt = env("ODEUM_LESSON_BROWSER_STT", "0");
    if (stt != "0" && stt != "1") throw std::runtime_error("ODEUM_LESSON_BROWSER_STT must be 0 or 1");
    config.browser_stt = stt == "1";
    config.web_access = relay::WebAccess::from_environment(config.port, "ODEUM_LESSON_PUBLIC_URL", "ODEUM_LESSON_ALLOWED_ORIGINS");
    return config;
}
}
