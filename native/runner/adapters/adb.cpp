#include "adb.hpp"
#include "../core/json.hpp"
#include "process.hpp"

namespace odeum::runner {
void run_adb(boost::asio::io_context& io, const std::string& adb, const std::optional<std::string>& serial, std::vector<std::string> args,
    std::function<void(AdbResult)> done) {
    ProcessSpec spec;
    spec.program = adb;
    if (serial && !serial->empty()) spec.args = {"-s", *serial};
    for (auto& a : args) spec.args.push_back(std::move(a));
    spec.hide_window = true;
    run_collect(io, std::move(spec), std::chrono::seconds(10), 64 * 1024, 4096, std::nullopt, [done = std::move(done)](CollectResult result) {
        AdbResult out;
        if (auto* error = std::get_if<SpawnError>(&result)) {
            out.stderr_text = error->what();
            return done(out);
        }
        auto& collected = std::get<CollectedOutput>(result);
        out.ok = !collected.timed_out && !collected.overflow && collected.status.code == 0;
        out.stdout_text = sanitize_utf8(collected.stdout_bytes);
        out.stderr_text = sanitize_utf8(collected.stderr_tail);
        done(out);
    });
}
}
