#include "cernere_client.hpp"
#include "http_client.hpp"
#include "log.hpp"
#include <boost/asio/post.hpp>

namespace odeum::runner {
namespace {
constexpr long long cache_ttl_ms = 300'000;
constexpr std::size_t cache_limit = 2048;
constexpr std::size_t in_flight_limit = 32;
}

CernereAuth::CernereAuth(boost::asio::io_context& io, AuthSettings settings, std::function<long long()> now_ms)
    : io_(io), settings_(std::move(settings)), now_(std::move(now_ms)) {}

void CernereAuth::verify(const std::string& token, std::function<void(std::optional<VerifiedUser>)> done) {
    const auto decision = decide(settings_, token);
    if (decision.kind != AuthDecision::Kind::ask_cernere) {
        boost::asio::post(io_, [done = std::move(done), user = decision.user] { done(user); });
        return;
    }
    const auto now = now_();
    if (auto it = cache_.find(token); it != cache_.end()) {
        if (it->second.expires_at > now) {
            boost::asio::post(io_, [done = std::move(done), user = it->second.user] { done(user); });
            return;
        }
        cache_.erase(it);
    }
    // Concurrent checks of the same token share one request.
    if (auto it = pending_.find(token); it != pending_.end()) {
        it->second.push_back(std::move(done));
        return;
    }
    if (pending_.size() >= in_flight_limit) {
        log::warn("cernere-auth", "too many verifications in flight — rejected");
        boost::asio::post(io_, [done = std::move(done)] { done(std::nullopt); });
        return;
    }
    pending_[token].push_back(std::move(done));
    HttpRequest request;
    request.method = "POST";
    request.url = settings_.cernere_url + "/api/auth/verify";
    request.headers = {{"content-type", "application/json"}, {"authorization", "Bearer " + token}};
    request.body = dump(Json{{"token", token}});
    request.timeout = std::chrono::milliseconds(10'000);
    request.body_limit = service_response_limits.bytes;
    http_request(io_, std::move(request), [this, token](HttpResult result) {
        std::optional<VerifiedUser> user;
        if (auto* response = std::get_if<HttpResponse>(&result)) {
            if (!response->ok()) {
                log::warn("cernere-auth", "verify rejected", {{"status", response->status}});
            } else if (auto body = try_parse_json(response->body, service_response_limits)) {
                user = user_from_verify_response(*body);
            }
        } else {
            log::warn("cernere-auth", "verify failed", {{"error", std::get<HttpError>(result).message}});
        }
        if (user) {
            if (cache_.size() >= cache_limit) {
                const auto now = now_();
                for (auto it = cache_.begin(); it != cache_.end();) it = it->second.expires_at <= now ? cache_.erase(it) : std::next(it);
                if (cache_.size() >= cache_limit) cache_.erase(cache_.begin());
            }
            cache_[token] = Entry{*user, now_() + cache_ttl_ms};
        }
        auto waiting = std::move(pending_[token]);
        pending_.erase(token);
        for (auto& callback : waiting) callback(user);
    });
}
}
