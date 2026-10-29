#pragma once
// /ws broker (custos/src/ws/handler.ts): per-connection subscriptions, log / status / exit /
// screenshot / control / audience delivery, and browser input gated by the control lease.
#include "../adapters/cernere_client.hpp"
#include "../core/audience.hpp"
#include "../core/control_lease.hpp"
#include "../core/registry.hpp"
#include "apps_runner.hpp"
#include "http_types.hpp"
#include "input_forwarder.hpp"
#include "playlog_recorder.hpp"
#include "screenshot_streamer.hpp"
#include <set>

namespace odeum::runner {
struct WsBrokerDeps {
    AppsRegistry& registry;
    AppsRunner& runner;
    ScreenshotStreamer& streamer;
    PlayLogRecorder* play_log;  // optional
    ControlLease& control;
    AudienceRoom& audience;
    InputForwarder& input;
    CernereAuth& auth;
    std::function<long long()> now;
    std::function<std::string()> new_id;
};

class WsBroker final : public WsEndpoint {
public:
    explicit WsBroker(WsBrokerDeps deps);
    void authorize(const ServerRequest& upgrade, std::function<void(std::optional<VerifiedUser>)> done) override;
    std::string open(std::shared_ptr<WsPeer> peer, const VerifiedUser& user) override;
    void message(const std::string& session_id, std::string_view text) override;
    void closed(const std::string& session_id) override;
    std::size_t sessions() const { return sessions_.size(); }
private:
    struct Session {
        std::string id;
        std::shared_ptr<WsPeer> peer;
        std::set<std::string> app_ids, audience_app_ids;
        std::optional<long long> last_audience_at, last_comment_at;
        VerifiedUser user;
    };
    WsBrokerDeps deps_;
    std::map<std::string, Session> sessions_;
    void send(Session& session, const Json& message, bool droppable = false);
    void broadcast(const std::string& app_id, const Json& message, bool droppable = false);
    void error(Session& session, const std::string& message);
    void handle(Session& session, const Json& message);
};
std::string base64(std::string_view bytes);
}
