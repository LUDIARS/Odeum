#include "playlog_recorder.hpp"
#include "../adapters/log.hpp"
#include "../adapters/paths.hpp"
#include <boost/asio/post.hpp>

namespace odeum::runner {
namespace {
constexpr long long pointer_move_min_ms = 100;
constexpr auto idle_check = std::chrono::milliseconds(5'000);
std::string run_key(const std::string& session_id, const std::string& app_id) { return session_id + std::string(1, '\0') + app_id; }
}

StatePoller::StatePoller(boost::asio::io_context& io, StateFetcher fetcher, Listener listener)
    : io_(io), fetcher_(std::move(fetcher)), listener_(std::move(listener)) {}

std::optional<Json> StatePoller::current(const std::string& app_id) const {
    auto it = latest_.find(app_id);
    return it == latest_.end() ? std::nullopt : it->second;
}

void StatePoller::acquire(const AppConfig& app, const std::string& ref) {
    if (!app.in_app_bridge || !app.play_log || unsupported_.contains(app.id)) return;
    if (auto it = entries_.find(app.id); it != entries_.end()) {
        it->second.refs.insert(ref);
        return;
    }
    Entry entry;
    entry.refs.insert(ref);
    entry.timer = std::make_unique<boost::asio::steady_timer>(io_);
    entry.endpoint = {app.in_app_bridge->host, app.in_app_bridge->port};
    entry.period = std::chrono::milliseconds(app.play_log->state_poll_ms);
    entries_.emplace(app.id, std::move(entry));
    schedule(app.id);
    tick(app.id);
}

void StatePoller::schedule(const std::string& app_id) {
    auto it = entries_.find(app_id);
    if (it == entries_.end()) return;
    auto* timer = it->second.timer.get();
    timer->expires_after(it->second.period);
    timer->async_wait([this, app_id, timer](const boost::system::error_code& error) {
        if (error) return;
        auto e = entries_.find(app_id);
        if (e == entries_.end() || e->second.timer.get() != timer) return;
        schedule(app_id);
        tick(app_id);
    });
}

void StatePoller::release(const std::string& app_id, const std::string& ref) {
    auto it = entries_.find(app_id);
    if (it == entries_.end()) return;
    it->second.refs.erase(ref);
    if (it->second.refs.empty()) stop(app_id);
}

void StatePoller::shutdown() {
    for (auto& [id, entry] : entries_) entry.timer->cancel();
    entries_.clear();
}

void StatePoller::stop(const std::string& app_id) {
    auto it = entries_.find(app_id);
    if (it == entries_.end()) return;
    it->second.timer->cancel();
    entries_.erase(it);
}

void StatePoller::tick(const std::string& app_id) {
    auto it = entries_.find(app_id);
    if (it == entries_.end() || it->second.inflight) return;
    it->second.inflight = true;
    auto* timer = it->second.timer.get();
    fetcher_(it->second.endpoint, [this, app_id, timer](std::variant<BridgeState, BridgeCallError> result) {
        auto e = entries_.find(app_id);
        const bool same = e != entries_.end() && e->second.timer.get() == timer;
        if (same) e->second.inflight = false;
        if (auto* error = std::get_if<BridgeCallError>(&result)) {
            if (error->kind == BridgeCallError::Kind::unsupported) {
                log::warn("playlog-state", "bridge has no /state — play log continues without game state", {{"appId", app_id}});
                unsupported_.insert(app_id);
                if (same) stop(app_id);
                return;
            }
            log::debug("playlog-state", "state poll failed", {{"appId", app_id}, {"msg", error->message}});
            return;
        }
        if (!same) return;
        const auto& state = std::get<BridgeState>(result).state;
        latest_[app_id] = state;
        listener_(app_id, state);
    });
}

PlayLogRecorder::PlayLogRecorder(boost::asio::io_context& io, PlayLogDeps deps)
    : io_(io), deps_(std::move(deps)),
      poller_(io, deps_.fetch_state, [this](const std::string& app_id, const std::optional<Json>& state) { on_state(app_id, state); }),
      idle_timer_(io) {
    arm_idle();
}

void PlayLogRecorder::arm_idle() {
    idle_timer_.expires_after(idle_check);
    idle_timer_.async_wait([this](const boost::system::error_code& error) {
        if (error || stopped_) return;
        close_idle();
        arm_idle();
    });
}

void PlayLogRecorder::record_input(const std::string& session_id, const std::string& user_id, const std::string& app_id, const PlayInput& input) {
    auto it = runs_.find(run_key(session_id, app_id));
    Run* run = it != runs_.end() ? &it->second : open(session_id, user_id, app_id);
    if (!run) return;
    const auto now = deps_.now();
    if (const auto* pointer = std::get_if<PointerInput>(&input)) {
        if (pointer->phase == "move") {
            if (!run->pointer_held || now - run->last_move_at < pointer_move_min_ms) return;
            run->last_move_at = now;
        } else {
            run->pointer_held = pointer->phase == "down";
        }
    }
    run->last_active = now;
    write(*run, "input", input_fields(input));
}

void PlayLogRecorder::record_comment(const std::string& app_id, const std::string& user_id, const std::string& text) {
    bool any = false;
    for (const auto& [key, run] : runs_) any |= run.app_id == app_id;
    if (!any) return;  // a comment alone does not open a run
    const auto commenter = deps_.hash_player(user_id);
    for (auto& [key, run] : runs_) if (run.app_id == app_id) write(run, "comment", Json{{"comment", text}, {"commenter", commenter}});
}

void PlayLogRecorder::close_session_app(const std::string& session_id, const std::string& app_id, const std::string& reason) {
    close(run_key(session_id, app_id), reason);
}

void PlayLogRecorder::close_session(const std::string& session_id, const std::string& reason) {
    std::vector<std::string> keys;
    for (const auto& [key, run] : runs_) if (run.session_id == session_id) keys.push_back(key);
    for (const auto& key : keys) close(key, reason);
}

void PlayLogRecorder::close_app(const std::string& app_id, const std::string& reason) {
    std::vector<std::string> keys;
    for (const auto& [key, run] : runs_) if (run.app_id == app_id) keys.push_back(key);
    for (const auto& key : keys) close(key, reason);
}

void PlayLogRecorder::close_idle() {
    const auto now = deps_.now();
    std::vector<std::string> keys;
    for (const auto& [key, run] : runs_) if (now - run.last_active >= run.idle_stop_ms) keys.push_back(key);
    for (const auto& key : keys) close(key, "idle");
}

void PlayLogRecorder::on_state(const std::string& app_id, const std::optional<Json>& state) {
    if (!state) return;
    const auto json = dump(*state);
    for (auto& [key, run] : runs_) {
        if (run.app_id != app_id || run.last_state_json == json) continue;
        run.last_active = deps_.now();
        write(run, "state", Json::object());
    }
}

void PlayLogRecorder::shutdown() {
    stopped_ = true;
    idle_timer_.cancel();
    std::vector<std::string> keys;
    for (const auto& [key, run] : runs_) keys.push_back(key);
    for (const auto& key : keys) close(key, "shutdown");
    poller_.shutdown();
}

PlayLogRecorder::Run* PlayLogRecorder::open(const std::string& session_id, const std::string& user_id, const std::string& app_id) {
    const auto* config = deps_.config(app_id);
    if (!config || !config->play_log || !config->play_log->enabled) return nullptr;
    const auto now = deps_.now();
    const auto id = make_run_id(now, deps_.pick);
    std::unique_ptr<PlayLogWriter> writer;
    try {
        writer = deps_.open_writer(from_path(to_path(deps_.dir) / to_path(app_id) / to_path(id + ".jsonl")));
    } catch (const std::exception& error) {
        log::warn("playlog", "play log open failed", {{"appId", app_id}, {"msg", error.what()}});
        return nullptr;
    }
    Run run;
    run.key = run_key(session_id, app_id);
    run.id = id;
    run.app_id = app_id;
    run.session_id = session_id;
    run.player = deps_.hash_player(user_id);
    run.started_at = run.last_active = now;
    run.idle_stop_ms = static_cast<long long>(config->play_log->idle_stop_sec) * 1000;
    run.writer = std::move(writer);
    auto [it, inserted] = runs_.emplace(run.key, std::move(run));
    poller_.acquire(*config, it->first);
    write(it->second, "start", Json{{"source", "ws"}});
    log::info("playlog", "play log started", {{"appId", app_id}, {"run", id}});
    return &it->second;
}

void PlayLogRecorder::close(const std::string& key, const std::string& reason) {
    auto it = runs_.find(key);
    if (it == runs_.end()) return;
    write(it->second, "end", Json{{"reason", reason}});
    auto run = std::move(it->second);
    runs_.erase(it);
    poller_.release(run.app_id, run.key);
    run.writer->close();
    log::info("playlog", "play log closed", {{"appId", run.app_id}, {"run", run.id}, {"reason", reason}, {"rows", run.seq}});
}

void PlayLogRecorder::write(Run& run, const std::string& kind, const Json& fields) {
    const auto now = deps_.now();
    const PlayLogRowBase base{run.id, run.player, run.app_id, run.seq, now - run.started_at, iso_time(now)};
    const auto state = poller_.current(run.app_id);
    if (state) run.last_state_json = dump(*state);
    try {
        run.writer->write(build_row(base, kind, fields, state));
        run.seq += 1;
    } catch (const std::exception& error) {
        log::warn("playlog", "play log write failed", {{"appId", run.app_id}, {"run", run.id}, {"msg", error.what()}});
    }
}
}
