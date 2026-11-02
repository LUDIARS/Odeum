#include "ice_provider.hpp"
#include "http_client.hpp"
#include "log.hpp"
#include <boost/asio/post.hpp>

namespace odeum::runner {
IceServerProvider::IceServerProvider(boost::asio::io_context& io, const std::optional<std::string>& stun_env, std::string turn_key_id,
    std::string turn_token, std::function<long long()> now_ms, std::string turn_api)
    : io_(io), stun_(stun_servers_from_env(stun_env)), turn_key_id_(std::move(turn_key_id)), turn_token_(std::move(turn_token)), turn_api_(std::move(turn_api)),
      now_(std::move(now_ms)) {
    Json urls = Json::array();
    for (const auto& s : stun_) for (const auto& u : s.urls) urls.push_back(u);
    log::info("ice-servers", "ICE servers configured", {{"stun", urls}, {"turn", has_turn()}});
    if (!has_turn()) log::info("ice-servers", "TURN is not configured — peers behind strict NAT (e.g. mobile CGNAT) may not receive video");
}

void IceServerProvider::get(std::function<void(std::vector<IceServer>)> done) {
    if (!has_turn()) return boost::asio::post(io_, [done, servers = stun_] { done(servers); });
    if (turn_ && turn_->second - turn_refresh_margin_ms > now_()) {
        auto servers = stun_;
        servers.insert(servers.end(), turn_->first.begin(), turn_->first.end());
        return boost::asio::post(io_, [done, servers] { done(servers); });
    }
    pending_.push_back(std::move(done));
    if (pending_.size() > 1) return;  // one issue request at a time
    HttpRequest request;
    request.method = "POST";
    request.url = turn_api_ + "/" + encode_uri_component(turn_key_id_) + "/credentials/generate-ice-servers";
    request.headers = {{"authorization", "Bearer " + turn_token_}, {"content-type", "application/json"}};
    request.body = dump(Json{{"ttl", turn_ttl_sec}});
    request.timeout = std::chrono::milliseconds(10'000);
    request.body_limit = service_response_limits.bytes;
    http_request(io_, std::move(request), [this](HttpResult result) {
        std::vector<IceServer> turn;
        std::string failure;
        if (auto* error = std::get_if<HttpError>(&result)) failure = error->message;
        else if (const auto& response = std::get<HttpResponse>(result); !response.ok()) failure = "Cloudflare TURN API returned HTTP " + std::to_string(response.status);
        else if (auto body = try_parse_json(response.body, service_response_limits)) turn = parse_turn_response(*body);
        if (failure.empty() && turn.empty()) failure = "Cloudflare TURN API returned no TURN servers";
        auto servers = stun_;
        if (failure.empty()) {
            turn_ = std::make_pair(turn, now_() + turn_ttl_sec * 1000);
            servers.insert(servers.end(), turn.begin(), turn.end());
        } else {
            log::warn("ice-servers", "TURN credentials unavailable — using STUN only", {{"err", failure}});
        }
        auto waiting = std::move(pending_);
        pending_.clear();
        for (auto& callback : waiting) callback(servers);
    });
}
}
