#pragma once
// ffmpeg as an external program (argument arrays, 8 s deadline, Lorica-charged output): one-frame
// desktop screenshots, PNG downscaling, and the H.264 encoder probe (screenshot.ts, scale.ts,
// ffmpeg-pipeline.ts resolveEncoder). The probe runs asynchronously instead of blocking.
#include "../core/ffmpeg_args.hpp"
#include <boost/asio/io_context.hpp>
#include <functional>
#include <variant>

namespace odeum::runner {
using PngResult = std::variant<std::string, std::string>;  // index 0: PNG bytes, index 1: error message
inline bool png_ok(const PngResult& r) { return r.index() == 0; }

class MediaTools {
public:
    MediaTools(boost::asio::io_context& io, std::string ffmpeg, CaptureHost host, std::string encoder_setting);
    const std::string& ffmpeg() const { return ffmpeg_; }
    const CaptureHost& host() const { return host_; }
    void capture_png(const AppConfig& app, std::function<void(PngResult)> done);
    void scale_png(std::string png, std::optional<long long> max_width, std::function<void(PngResult)> done);
    // The encoder in use: forced by CUSTOS_VIDEO_ENCODER or the first that encodes in a probe.
    void encoder(std::function<void(Encoder)> done);
    bool forced() const { return forced_.has_value(); }
private:
    boost::asio::io_context& io_;
    std::string ffmpeg_;
    CaptureHost host_;
    std::optional<Encoder> forced_, resolved_;
    std::vector<std::function<void(Encoder)>> waiting_;
    bool probing_ = false;
    void probe(std::size_t index);
};
}
