#include "media_tools.hpp"
#include "log.hpp"
#include "process.hpp"
#include <boost/asio/post.hpp>
#include <sstream>

namespace odeum::runner {
namespace {
constexpr auto timeout = std::chrono::milliseconds(8'000);

// The last `count` lines of an stderr tail, joined with " | " (the former error messages).
std::string tail_lines(const std::string& tail, std::size_t count) {
    std::vector<std::string> lines;
    std::stringstream in(sanitize_utf8(tail));
    std::string line;
    while (std::getline(in, line, '\n')) lines.push_back(line);
    if (!tail.empty() && tail.back() == '\n') lines.emplace_back();
    std::string out;
    const std::size_t first = lines.size() > count ? lines.size() - count : 0;
    for (std::size_t i = first; i < lines.size(); ++i) out += (i > first ? " | " : "") + lines[i];
    return out;
}

std::string exit_text(const ExitStatus& status) { return status.code ? std::to_string(*status.code) : "null"; }
}

MediaTools::MediaTools(boost::asio::io_context& io, std::string ffmpeg, CaptureHost host, std::string encoder_setting)
    : io_(io), ffmpeg_(std::move(ffmpeg)), host_(std::move(host)), forced_(forced_encoder(encoder_setting)) {}

void MediaTools::capture_png(const AppConfig& app, std::function<void(PngResult)> done) {
    if (!app.capture) return boost::asio::post(io_, [done, id = app.id] { done(PngResult(std::in_place_index<1>, "app " + id + " has no capture config")); });
    std::vector<std::string> args;
    try {
        args = screenshot_args(*app.capture, host_);
    } catch (const std::exception& error) {
        return boost::asio::post(io_, [done, message = std::string(error.what())] { done(PngResult(std::in_place_index<1>, message)); });
    }
    log::info("screenshot", "screenshot spawn", {{"appId", app.id}});
    ProcessSpec spec{ffmpeg_, args, {}, {}, false, true, true, true};
    run_collect(io_, std::move(spec), timeout, max_bridge_png_bytes, 2000, std::nullopt, [done = std::move(done)](CollectResult result) {
        if (auto* error = std::get_if<SpawnError>(&result)) return done(PngResult(std::in_place_index<1>, std::string("ffmpeg spawn failed: ") + error->what()));
        auto& out = std::get<CollectedOutput>(result);
        if (out.timed_out) return done(PngResult(std::in_place_index<1>, "ffmpeg screenshot timeout (8000ms)"));
        if (out.overflow) return done(PngResult(std::in_place_index<1>, "ffmpeg screenshot output too large"));
        if (out.status.code == 0 && !out.stdout_bytes.empty()) return done(PngResult(std::in_place_index<0>, std::move(out.stdout_bytes)));
        const auto tail = tail_lines(out.stderr_tail, 5);
        done(PngResult(std::in_place_index<1>, "ffmpeg exit " + exit_text(out.status) + " — " + (tail.empty() ? "no stderr" : tail)));
    });
}

void MediaTools::scale_png(std::string png, std::optional<long long> max_width, std::function<void(PngResult)> done) {
    if (!max_width || *max_width <= 0) return boost::asio::post(io_, [done, png = std::move(png)]() mutable { done(PngResult(std::in_place_index<0>, std::move(png))); });
    if (const auto width = read_png_width(png); width && *width <= *max_width) {
        return boost::asio::post(io_, [done, png = std::move(png)]() mutable { done(PngResult(std::in_place_index<0>, std::move(png))); });
    }
    ProcessSpec spec{ffmpeg_, scale_args(*max_width), {}, {}, true, true, true, true};
    run_collect(io_, std::move(spec), timeout, max_bridge_png_bytes, 1500, std::move(png), [done = std::move(done)](CollectResult result) {
        if (auto* error = std::get_if<SpawnError>(&result)) return done(PngResult(std::in_place_index<1>, std::string("ffmpeg scale spawn failed: ") + error->what()));
        auto& out = std::get<CollectedOutput>(result);
        if (out.timed_out) return done(PngResult(std::in_place_index<1>, "ffmpeg scale timeout (8000ms)"));
        if (out.overflow) return done(PngResult(std::in_place_index<1>, "ffmpeg scale output too large"));
        if (out.status.code == 0 && !out.stdout_bytes.empty()) return done(PngResult(std::in_place_index<0>, std::move(out.stdout_bytes)));
        const auto tail = tail_lines(out.stderr_tail, 3);
        done(PngResult(std::in_place_index<1>, "ffmpeg scale exit " + exit_text(out.status) + " — " + (tail.empty() ? "no stderr" : tail)));
    });
}

void MediaTools::encoder(std::function<void(Encoder)> done) {
    if (forced_) return boost::asio::post(io_, [done, e = *forced_] { done(e); });
    if (resolved_) return boost::asio::post(io_, [done, e = *resolved_] { done(e); });
    waiting_.push_back(std::move(done));
    if (probing_) return;
    probing_ = true;
    probe(0);
}

void MediaTools::probe(std::size_t index) {
    const auto order = probe_order();
    if (index >= order.size()) {
        resolved_ = Encoder::x264;
        log::warn("ffmpeg-capture", "no encoder probe succeeded — using libx264", {{"encoder", "x264"}, {"mode", "fallback"}});
        auto waiting = std::move(waiting_);
        for (auto& callback : waiting) callback(*resolved_);
        return;
    }
    const auto candidate = order[index];
    ProcessSpec spec{ffmpeg_, probe_args(candidate), {}, {}, false, true, true, true};
    run_collect(io_, std::move(spec), timeout, 64 * 1024, 200, std::nullopt, [this, candidate, index](CollectResult result) {
        auto* out = std::get_if<CollectedOutput>(&result);
        if (out && !out->timed_out && out->status.code == 0) {
            resolved_ = candidate;
            log::info("ffmpeg-capture", "video encoder selected (probed)", {{"encoder", encoder_name(candidate)}, {"mode", "auto"}});
            auto waiting = std::move(waiting_);
            for (auto& callback : waiting) callback(candidate);
            return;
        }
        log::info("ffmpeg-capture", "encoder probe failed", {{"encoder", encoder_name(candidate)}});
        probe(index + 1);
    });
}
}
