// Boundary inputs on the remaining input paths: files read by the runner, the bridge screenshot body,
// the bridge stream error body, play-log listing through symlinks, and budgets of the farm files.
#include "runner_check.hpp"
#include "runner_fake_http.hpp"
#include <adapters/bridge_client.hpp>
#include <adapters/http_client.hpp>
#include <adapters/playlog_files.hpp>
#include <core/farm.hpp>
#include <filesystem>
#include <fstream>
#include <unistd.h>

using namespace odeum::runner;
using namespace runner_test;
namespace fs = std::filesystem;

int main(int argc, char** argv) {
    return run_all(argc, argv, "runner.boundary", {
        {"files > read_file_limited: exactly the limit is read, limit + 1 is refused, directories refused", [] {
            const auto dir = fs::temp_directory_path() / ("odeum-boundary-" + std::to_string(::getpid()));
            fs::create_directories(dir);
            std::ofstream(dir / "exact", std::ios::binary) << std::string(1000, 'x');
            std::ofstream(dir / "over", std::ios::binary) << std::string(1001, 'x');
            check(read_file_limited((dir / "exact").string(), 1000)->size() == 1000, "exact");
            check(!read_file_limited((dir / "over").string(), 1000), "limit + 1");
            check(!read_file_limited(dir.string(), 1000) && !read_file_limited((dir / "missing").string(), 1000), "not a regular file");
            fs::remove_all(dir);
        }},
        {"play logs > a symlinked app directory is not listed", [] {
            const auto dir = fs::temp_directory_path() / ("odeum-boundary-links-" + std::to_string(::getpid()));
            const auto outside = dir.string() + "-outside";
            fs::create_directories(dir);
            fs::create_directories(outside);
            std::ofstream(fs::path(outside) / "20261011-000000-abcdef.jsonl") << "secret\n";
            fs::create_directory_symlink(outside, dir / "linked");
            check(list_play_logs(dir.string(), std::nullopt).empty(), "symlinked app directory skipped when listing everything");
            fs::remove_all(dir);
            fs::remove_all(outside);
        }},
        {"bridge > a /screenshot body beyond 32 MiB is refused before it is kept", [] {
            boost::asio::io_context io;
            FakeHttpServer bridge(io, [](const FakeHttpServer::Request&) {
                std::string png("\x89PNG\r\n\x1a\n", 8);
                png.resize(max_bridge_png_bytes + 1, 'x');
                return FakeResponse{200, png, {}, {}};
            });
            std::optional<std::variant<std::string, BridgeCallError>> shot;
            bridge_fetch_screenshot(io, {"127.0.0.1", bridge.port()}, [&](auto r) { shot = std::move(r); });
            run_until(io, [&] { return shot.has_value(); }, std::chrono::seconds(20));
            check(shot && std::holds_alternative<BridgeCallError>(*shot) && std::get<BridgeCallError>(*shot).message == "response body too large", "too large");
        }},
        {"bridge > the error body of a refused stream is bounded (500 bytes)", [] {
            boost::asio::io_context io;
            FakeHttpServer bridge(io, [](const FakeHttpServer::Request&) { return FakeResponse{415, std::string(100'000, 'e'), {}, {}}; });
            std::optional<std::string> body;
            std::shared_ptr<HttpStream> stream;
            HttpStream::Handlers h;
            h.on_headers = [](unsigned status, const std::map<std::string, std::string>&) { return status == 200; };
            h.on_data = [](std::string_view) { return true; };
            h.on_end = [&](const std::optional<HttpError>&) { body = stream->error_body(); };
            stream = HttpStream::open(io, "http://127.0.0.1:" + std::to_string(bridge.port()) + "/stream?codec=h264", std::chrono::seconds(5), 500, h);
            run_until(io, [&] { return body.has_value(); });
            check(body && body->size() == 500, "bounded error body");
        }},
        {"farm > configuration budget and limits on hosts / devices / tags", [] {
            Json config = Json::parse(R"({"version":1,"hosts":[{"id":"h","tokenEnv":"HOST_TOKEN"}],"devices":[]})");
            for (int i = 0; i < 1000; ++i) config["devices"].push_back(Json{{"id", "d" + std::to_string(i)}, {"hostId", "h"}, {"platform", "android"}, {"serial", "s" + std::to_string(i)}, {"model", "m"}, {"osVersion", "1"}});
            check(parse_farm_config(config).devices.size() == 1000, "1000 devices accepted");
            config["devices"].push_back(Json{{"id", "extra"}, {"hostId", "h"}, {"platform", "android"}, {"serial", "x"}, {"model", "m"}, {"osVersion", "1"}});
            throws([&] { parse_farm_config(config); }, "1001 devices refused");
            Json tags = Json::parse(R"({"version":1,"hosts":[{"id":"h","tokenEnv":"HOST_TOKEN"}],"devices":[{"id":"d","hostId":"h","platform":"ios","serial":"s","model":"m","osVersion":"1","tags":[]}]})");
            for (int i = 0; i < 32; ++i) tags["devices"][0]["tags"].push_back("t");
            parse_farm_config(tags);
            tags["devices"][0]["tags"].push_back("t");
            throws([&] { parse_farm_config(tags); }, "33 tags refused");
            Json report{{"devices", Json::array()}};
            for (int i = 0; i < 1001; ++i) report["devices"].push_back(Json{{"id", "d"}, {"connected", true}});
            throws([&] { parse_host_report(report); }, "1001 observations refused");
        }},
    });
}
