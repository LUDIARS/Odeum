// custos/tests/playlog-row.test.ts, playlog-recorder.test.ts + golden (playlog_cases).
#include "runner_check.hpp"
#include "runner_fake_http.hpp"
#include <adapters/playlog_files.hpp>
#include <app/playlog_recorder.hpp>
#include <filesystem>
#include <fstream>

using namespace odeum::runner;
using namespace runner_test;
namespace fs = std::filesystem;

namespace {
AppConfig app(std::optional<PlayLogConfig> play_log, bool bridge = false) {
    AppConfig a;
    a.id = "demo";
    a.name = "Demo";
    a.run.working_dir = ".";
    a.run.cmd = "x";
    a.play_log = play_log;
    if (bridge) { a.in_app_bridge = InAppBridgeConfig{}; a.in_app_bridge->kind = "unity"; a.in_app_bridge->port = 17778; }
    return a;
}

struct Harness {
    boost::asio::io_context io;
    long long clock = 1'000'000;
    AppConfig config;
    std::map<std::string, std::shared_ptr<std::vector<Json>>> files;
    std::vector<std::string> closed;
    std::optional<Json> state;
    std::unique_ptr<PlayLogRecorder> recorder;
    explicit Harness(AppConfig c, std::optional<Json> s = std::nullopt) : config(std::move(c)), state(std::move(s)) {
        class Writer final : public PlayLogWriter {
        public:
            Writer(std::shared_ptr<std::vector<Json>> rows, std::vector<std::string>& closed, std::string path) : rows_(std::move(rows)), closed_(closed), path_(std::move(path)) {}
            void write(const Json& row) override { rows_->push_back(row); }
            void close() override { closed_.push_back(path_); }
        private:
            std::shared_ptr<std::vector<Json>> rows_;
            std::vector<std::string>& closed_;
            std::string path_;
        };
        recorder = std::make_unique<PlayLogRecorder>(io, PlayLogDeps{
            "/plays",
            [this](const std::string& id) -> const AppConfig* { return id == config.id ? &config : nullptr; },
            [](const std::string& id) { return "h(" + id + ")"; },
            [this] { return clock; },
            [this](const std::string& path) {
                auto rows = std::make_shared<std::vector<Json>>();
                files[path] = rows;
                return std::make_unique<Writer>(rows, closed, path);
            },
            [](std::uint32_t) { return 0u; },
            // Asynchronous like the bridge: the first state arrives after the run has started.
            [this](const BridgeEndpoint&, auto done) { boost::asio::post(io, [done, s = state] { done(BridgeState{true, s}); }); }});
    }
    ~Harness() { recorder->shutdown(); }
    std::vector<Json>& rows() { return *files.begin()->second; }
    std::vector<std::string> kinds() { std::vector<std::string> k; for (auto& r : rows()) k.push_back(r["kind"]); return k; }
};
PlayLogConfig enabled() { return PlayLogConfig{true, 500, 300}; }
}

int main(int argc, char** argv) {
    return run_all(argc, argv, "runner.playlog", {
        {"playlog-row > golden input names, fields, rows, run ids, hashes", [] {
            const auto g = fixture("playlog_cases.json");
            for (const auto& c : g["inputs"]) {
                const auto& in = c["input"];
                PlayInput input;
                if (in["device"] == "key") input = KeyInput{in["key"], in["down"]};
                else if (in["device"] == "button") input = ButtonInput{in["id"]};
                else input = PointerInput{in["x"], in["y"], in["button"], in["phase"]};
                check(input_name(input) == c["name"].get<std::string>(), "name " + in.dump());
                check(dump(input_fields(input)) == c["fields"].dump(), "fields " + dump(input_fields(input)) + " vs " + c["fields"].dump());
            }
            for (const auto& c : g["rows"]) {
                const auto& b = c["base"];
                std::optional<Json> state;
                if (!c["state"].is_null()) state = c["state"];
                const auto row = build_row(PlayLogRowBase{b["run"], b["player"], b["app"], b["seq"], b["t_ms"], b["at"]}, c["kind"], c["fields"], state);
                check(dump(row) == c["output"].dump(), "row " + dump(row));
            }
            for (const auto& c : g["runIds"]) {
                std::vector<std::uint32_t> picks;
                for (const auto& p : c["picks"]) picks.push_back(p.get<std::uint32_t>());
                std::size_t i = 0;
                check(make_run_id(c["ms"], [&](std::uint32_t) { return picks.at(i++); }) == c["id"].get<std::string>(), "run id " + c.dump());
                check(iso_time(c["ms"]) == c["iso"].get<std::string>(), "iso " + c.dump());
            }
            for (const auto& c : g["runIdPattern"]) check(is_valid_run_id(c["id"].get<std::string>()) == c["ok"].get<bool>(), "pattern " + c.dump());
            for (const auto& c : g["hashes"]) check(hash_player(c["salt"].get<std::string>(), c["user"].get<std::string>()) == c["hash"].get<std::string>(), "hash " + c.dump());
        }},
        {"PlayLogRecorder > does nothing when the app has no playLog", [] {
            Harness h(app(std::nullopt));
            h.recorder->record_input("s1", "user", "demo", KeyInput{"W", true});
            check(h.files.empty() && h.recorder->open_runs() == 0, "nothing recorded");
        }},
        {"PlayLogRecorder > opens a run on the first input and closes it with an end row", [] {
            Harness h(app(enabled()));
            h.recorder->record_input("s1", "user-1", "demo", KeyInput{"W", true});
            h.clock += 250;
            h.recorder->record_input("s1", "user-1", "demo", KeyInput{"W", false});
            h.recorder->close_session("s1", "disconnect");
            check((h.kinds() == std::vector<std::string>{"start", "input", "input", "end"}), "kinds");
            check(h.rows()[2]["input"] == "key:W:up" && h.rows()[2]["t_ms"] == 250 && h.rows()[2]["player"] == "h(user-1)", "row 2");
            check(h.rows()[3]["reason"] == "disconnect" && h.rows()[3]["seq"] == 3, "end row");
            std::string all;
            for (auto& r : h.rows()) all += dump(r);
            check(all.find("\"user-1\"") == std::string::npos, "raw user id never written");
            const auto path = h.files.begin()->first;
            check(path.rfind("/plays/demo/", 0) == 0 && path.size() == std::string("/plays/demo/").size() + 22 + 6, "path " + path);
            check(h.closed.size() == 1 && h.recorder->open_runs() == 0, "closed");
        }},
        {"PlayLogRecorder > writes a spectator comment into the open run of the app with a hashed commenter", [] {
            Harness h(app(enabled()));
            h.recorder->record_comment("demo", "viewer-1", "いいね");
            check(h.files.empty(), "no run, no file");
            h.recorder->record_input("s1", "player-1", "demo", KeyInput{"W", true});
            h.clock += 40;
            h.recorder->record_comment("demo", "viewer-1", "いいね");
            h.recorder->record_comment("other", "viewer-1", "別 app");
            check((h.kinds() == std::vector<std::string>{"start", "input", "comment"}), "kinds");
            check(h.rows()[2]["comment"] == "いいね" && h.rows()[2]["commenter"] == "h(viewer-1)" && h.rows()[2]["t_ms"] == 40, "comment row");
        }},
        {"PlayLogRecorder > records pointer moves only while pressed and at most every 100ms", [] {
            Harness h(app(enabled()));
            PointerInput move{0.1, 0.2, "left", "move"};
            auto with = [&](const char* phase) { auto p = move; p.phase = phase; return p; };
            h.recorder->record_input("s1", "u", "demo", move);
            h.recorder->record_input("s1", "u", "demo", with("down"));
            h.clock += 150;
            h.recorder->record_input("s1", "u", "demo", move);
            h.clock += 50;
            h.recorder->record_input("s1", "u", "demo", move);
            h.recorder->record_input("s1", "u", "demo", with("up"));
            h.clock += 200;
            h.recorder->record_input("s1", "u", "demo", move);
            std::vector<std::string> names;
            for (auto& r : h.rows()) names.push_back(r.contains("input") ? r["input"].get<std::string>() : r["kind"].get<std::string>());
            check((names == std::vector<std::string>{"start", "pointer:left", "pointer:move", "pointer:left:up"}), "throttled");
        }},
        {"PlayLogRecorder > writes state rows only when the game state changes and stamps state columns", [] {
            Harness h(app(enabled(), true), Json{{"stage", "1-1"}});
            h.recorder->record_input("s1", "u", "demo", ButtonInput{"jump"});
            h.io.restart();
            h.io.poll();  // the first poll after opening
            h.recorder->on_state("demo", Json{{"stage", "1-1"}});
            h.recorder->record_input("s1", "u", "demo", KeyInput{"Space", true});
            const auto kinds = h.kinds();
            check((kinds == std::vector<std::string>{"start", "input", "state", "input"}), "kinds " + Json(kinds).dump());
            check(h.rows()[2]["state.stage"] == "1-1" && h.rows()[3]["state.stage"] == "1-1", "state columns");
        }},
        {"PlayLogRecorder > closes idle runs and runs of an exited app", [] {
            auto config = app(PlayLogConfig{true, 500, 10});
            Harness h(config);
            h.recorder->record_input("s1", "u", "demo", KeyInput{"A", true});
            h.clock += 9000;
            h.recorder->close_idle();
            check(h.recorder->open_runs() == 1, "not idle yet");
            h.clock += 1000;
            h.recorder->close_idle();
            check(h.recorder->open_runs() == 0 && h.rows().back()["reason"] == "idle", "idle");
            h.recorder->record_input("s2", "u", "demo", KeyInput{"A", true});
            h.recorder->close_app("demo", "exit");
            check(h.recorder->open_runs() == 0, "exit");
        }},
        {"PlayLogRecorder > keeps separate runs per session", [] {
            Harness h(app(enabled()));
            h.recorder->record_input("s1", "a", "demo", KeyInput{"A", true});
            h.recorder->record_input("s2", "b", "demo", KeyInput{"B", true});
            check(h.recorder->open_runs() == 2, "two runs");
            h.recorder->close_session_app("s1", "demo", "unsubscribe");
            check(h.recorder->open_runs() == 1, "one left");
        }},
        {"play log store and routes > lists only valid run files and reads them back; ids are validated", [] {
            const auto dir = fs::temp_directory_path() / ("odeum-plays-" + std::to_string(::getpid()));
            fs::remove_all(dir);
            fs::create_directories(dir / "demo");
            std::ofstream(dir / "demo" / "20261011-000000-abcdef.jsonl") << "{\"seq\":0}\n";
            std::ofstream(dir / "demo" / "notes.txt") << "x";
            std::ofstream(dir / ".salt") << "s";
            const auto list = list_play_logs(dir.string(), std::nullopt);
            check(list.size() == 1 && list[0].run == "20261011-000000-abcdef" && list[0].bytes == 10, "listed");
            check(list_play_logs(dir.string(), std::string("other")).empty() && list_play_logs(dir.string(), std::string("../x")).empty(), "filters");
            check(read_play_log(dir.string(), "demo", "20261011-000000-abcdef") == std::string("{\"seq\":0}\n"), "read");
            check(!read_play_log(dir.string(), "..", "20261011-000000-abcdef") && !read_play_log(dir.string(), "demo", "../.salt"), "traversal rejected");
            const auto salt = resolve_player_salt((dir / "fresh").string(), std::nullopt);
            check(salt.size() == 64 && resolve_player_salt((dir / "fresh").string(), std::nullopt) == salt, "salt created once and reused");
            check((fs::status(dir / "fresh" / ".salt").permissions() & fs::perms::group_all) == fs::perms::none, "salt file not group-readable (0600)");
            check(resolve_player_salt(dir.string(), std::string("env")) == "env", "env salt wins");
            auto writer = open_play_log_file((dir / "demo" / "20261011-000001-abcdef.jsonl").string());
            writer->write(Json{{"a", 1}});
            writer->close();
            throws([&] { open_play_log_file((dir / "demo" / "20261011-000001-abcdef.jsonl").string()); }, "existing file is never reused");
            fs::remove_all(dir);
        }},
    });
}
