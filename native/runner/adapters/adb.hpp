#pragma once
// Android input through `adb shell input keyevent|tap` (custos/src/input/android.ts). adb runs with
// an argument array; added: a 10 s deadline and an output budget.
#include <boost/asio/io_context.hpp>
#include <functional>
#include <optional>
#include <string>

namespace odeum::runner {
struct AdbResult {
    bool ok = false;
    std::string stdout_text, stderr_text;
};
void run_adb(boost::asio::io_context& io, const std::string& adb, const std::optional<std::string>& serial, std::vector<std::string> args,
    std::function<void(AdbResult)> done);
}
