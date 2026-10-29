#pragma once
// `unityctl` argument parsing, rendering and exit codes (custos/src/unity/unityctl-args.ts,
// unityctl-render.ts). Pure; odeum-unityctl performs the I/O.
#include "json.hpp"
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace odeum::runner {
inline constexpr int exit_ok = 0;
inline constexpr int exit_compile_error = 1;
inline constexpr int exit_unreachable = 2;

class UnityctlArgError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

struct UnityctlArgs {
    std::string command;  // status | health | compile | errors | log | play | stop | scene | hierarchy | capture
    std::string host = "127.0.0.1";
    std::optional<int> port;
    std::string token;
    bool json = false, wait = false;
    long long since = 0;
    std::string level;
    long long limit = 200, depth = 2;
    std::string root, caption, source = "gameview";
};
UnityctlArgs parse_unityctl_args(const std::vector<std::string>& argv);
// "host:port" | "port" | "host"
std::pair<std::string, std::optional<int>> split_host_port(const std::string& value);
std::string unityctl_usage();

int compile_exit_code(const Json& compile_status);
std::string render_status(const Json& status);
std::string render_compile_status(const Json& status);
std::string format_compile_message(const Json& message);
std::string render_log(const Json& page);
// JSON.stringify(value, null, 2)
std::string render_json(const Json& value);
}
