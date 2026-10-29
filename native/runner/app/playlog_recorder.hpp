#pragma once
// Play-log runs (custos/src/playlog/playlog-recorder.ts, state-poller.ts): one run per (WebSocket
// session, app), opened by the first input and closed by unsubscribe / disconnect / idle / app exit /
// shutdown with an `end` row. Game state comes from the bridge GET /state, polled while a run is open.
#include "../adapters/bridge_client.hpp"
#include "../adapters/playlog_files.hpp"
#include "../core/app_config.hpp"
#include "../core/playlog_row.hpp"
#include <boost/asio/steady_timer.hpp>
#include <map>
#include <set>

namespace odeum::runner {
using StateFetcher = std::function<void(const BridgeEndpoint&, std::function<void(std::variant<BridgeState, BridgeCallError>)>)>;

class StatePoller {
public:
    using Listener = std::function<void(const std::string& app_id, const std::optional<Json>& state)>;
    StatePoller(boost::asio::io_context& io, StateFetcher fetcher, Listener listener);
    std::optional<Json> current(const std::string& app_id) const;
    void acquire(const AppConfig& app, const std::string& ref);
    void release(const std::string& app_id, const std::string& ref);
    void shutdown();
private:
    struct Entry {
        std::set<std::string> refs;
        std::unique_ptr<boost::asio::steady_timer> timer;
        bool inflight = false;
        BridgeEndpoint endpoint;
        std::chrono::milliseconds period{500};
    };
    boost::asio::io_context& io_;
    StateFetcher fetcher_;
    Listener listener_;
    std::map<std::string, Entry> entries_;
    std::map<std::string, std::optional<Json>> latest_;
    std::set<std::string> unsupported_;
    void schedule(const std::string& app_id);
    void tick(const std::string& app_id);
    void stop(const std::string& app_id);
};

struct PlayLogDeps {
    std::string dir;
    std::function<const AppConfig*(const std::string&)> config;
    std::function<std::string(const std::string&)> hash_player;
    std::function<long long()> now;
    std::function<std::unique_ptr<PlayLogWriter>(const std::string& path)> open_writer;
    std::function<std::uint32_t(std::uint32_t)> pick;  // run id suffix
    StateFetcher fetch_state;
};

class PlayLogRecorder {
public:
    PlayLogRecorder(boost::asio::io_context& io, PlayLogDeps deps);
    void record_input(const std::string& session_id, const std::string& user_id, const std::string& app_id, const PlayInput& input);
    void record_comment(const std::string& app_id, const std::string& user_id, const std::string& text);
    void close_session_app(const std::string& session_id, const std::string& app_id, const std::string& reason);
    void close_session(const std::string& session_id, const std::string& reason);
    void close_app(const std::string& app_id, const std::string& reason);
    void close_idle();
    void on_state(const std::string& app_id, const std::optional<Json>& state);
    void shutdown();
    std::size_t open_runs() const { return runs_.size(); }
private:
    struct Run {
        std::string key, id, app_id, session_id, player;
        long long started_at = 0, last_active = 0, idle_stop_ms = 0, seq = 0, last_move_at = 0;
        std::unique_ptr<PlayLogWriter> writer;
        std::optional<std::string> last_state_json;
        bool pointer_held = false;
    };
    boost::asio::io_context& io_;
    PlayLogDeps deps_;
    StatePoller poller_;
    boost::asio::steady_timer idle_timer_;
    std::map<std::string, Run> runs_;
    bool stopped_ = false;
    Run* open(const std::string& session_id, const std::string& user_id, const std::string& app_id);
    void close(const std::string& key, const std::string& reason);
    void write(Run& run, const std::string& kind, const Json& fields);
    void arm_idle();
};
}
