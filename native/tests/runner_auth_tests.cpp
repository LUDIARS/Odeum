// custos/tests/auth.test.ts + the bind guard of main.ts + Cernere verification against a fake server
// + the added web access checks and the outbound HTTP client limits.
#include "runner_check.hpp"
#include "runner_fake_http.hpp"
#include <adapters/cernere_client.hpp>
#include <adapters/http_client.hpp>
#include <core/auth_policy.hpp>
#include <core/web_access.hpp>

using namespace odeum::runner;
using namespace runner_test;

namespace {
std::optional<VerifiedUser> verify(CernereAuth& auth, boost::asio::io_context& io, const std::string& token) {
    std::optional<std::optional<VerifiedUser>> result;
    auth.verify(token, [&](std::optional<VerifiedUser> user) { result = user; });
    run_until(io, [&] { return result.has_value(); });
    check(result.has_value(), "verify completed");
    return *result;
}
}

int main(int argc, char** argv) {
    return run_all(argc, argv, "runner.auth", {
        {"CernereAuth > CUSTOS_OPEN=1 → returns dev anon for any token", [] {
            check(decide({true, "", false}, "").user->id == "dev-anon", "open");
            check(decide({true, "http://c", true}, "").user->id == "dev-anon", "open wins over everything");
        }},
        {"CernereAuth > no CERNERE_URL → anonymous (open) mode by default", [] {
            check(decide({}, "").user->id == "anon" && decide({}, "anything").user->id == "anon", "anonymous");
        }},
        {"CernereAuth > CUSTOS_AUTH_REQUIRED=1 + no CERNERE_URL → stub-token-required", [] {
            check(decide({false, "", true}, "").kind == AuthDecision::Kind::reject, "empty token rejected");
            check(decide({false, "", true}, "anything").user->id == "stub", "stub");
        }},
        {"CernereAuth > enabled is true only when CERNERE_URL is set", [] {
            check(auth_mode({}) == AuthMode::anonymous, "anonymous");
            check(auth_mode({false, "http://localhost:8080", false}) == AuthMode::cernere, "cernere");
            check(decide({false, "http://x", false}, "").kind == AuthDecision::Kind::reject, "cernere requires a token");
            check(describe({false, "http://x", false}) == "cernere (http://x)", "describe");
        }},
        {"middleware > Bearer parsing follows /^Bearer\\s+(.+)$/i", [] {
            check(bearer_token("Bearer abc") == "abc" && bearer_token("bearer\tabc def") == "abc def", "parsed");
            check(bearer_token("Bearer ").empty() && bearer_token("Basic abc").empty() && bearer_token("Bearerabc").empty(), "rejected");
            check(bearer_token("Bearer  ") == " ", "regex backtracking keeps one character");
        }},
        {"cernere-auth > verify response parsing", [] {
            check(user_from_verify_response(Json{{"user", {{"id", "u1"}, {"name", "N"}}}})->role == "general", "defaults");
            check(user_from_verify_response(Json{{"user", {{"id", 42}}}})->id == "42", "numeric id");
            check(!user_from_verify_response(Json{{"user", {{"id", ""}}}}), "empty id");
            check(!user_from_verify_response(Json{{"user", nullptr}}) && !user_from_verify_response(Json::array()), "no user");
        }},
        {"main > refuses a non-loopback bind without authentication", [] {
            check(refuses_bind("0.0.0.0", {}), "anonymous wildcard refused");
            check(refuses_bind("0.0.0.0", {true, "http://c", false}), "CUSTOS_OPEN refused even with Cernere");
            check(!refuses_bind("0.0.0.0", {false, "http://c", false}), "Cernere allows LAN");
            check(!refuses_bind("0.0.0.0", {false, "", true}), "stub token allows LAN (as before)");
            check(!refuses_bind("127.0.0.1", {}) && !refuses_bind("::1", {}) && !refuses_bind("localhost", {}), "loopback allowed");
        }},
        {"web access > Host allowlist on loopback and same-origin for state changes", [] {
            WebAccessPolicy policy{true, {".example.org"}, {"https://ui.example.org"}, "127.0.0.1"};
            check(host_allowed(policy, "localhost:7676") && host_allowed(policy, "127.0.0.1:4649") && host_allowed(policy, "[::1]:7676"), "loopback hosts");
            check(host_allowed(policy, "lab.example.org") && host_allowed(policy, "example.org:443"), "allowed suffix");
            check(!host_allowed(policy, "evil.test") && !host_allowed(policy, "") && !host_allowed(policy, "a@b") && !host_allowed(policy, "x:99999"), "rejected hosts");
            check(origin_allowed(policy, "", "localhost:7676") && origin_allowed(policy, "http://localhost:7676", "localhost:7676"), "same origin");
            check(origin_allowed(policy, "https://ui.example.org", "localhost:7676"), "configured origin");
            check(!origin_allowed(policy, "http://evil.test", "localhost:7676") && !origin_allowed(policy, "null", "localhost:7676"), "cross origin");
            check(is_state_changing("POST") && is_state_changing("PUT") && !is_state_changing("GET") && !is_state_changing("HEAD"), "methods");
            WebAccessPolicy lan{false, {}, {}, "0.0.0.0"};
            check(host_allowed(lan, "192.168.1.5:7676"), "no host pinning when bound beyond loopback (authentication is mandatory there)");
        }},
        {"CernereAuth > verifies against /api/auth/verify, caches 5 minutes and shares concurrent checks", [] {
            boost::asio::io_context io;
            FakeHttpServer server(io, [](const FakeHttpServer::Request& r) {
                if (r[boost::beast::http::field::authorization] == "Bearer good") return FakeResponse{200, R"({"user":{"id":"u1","name":"Alice","email":"a@x","role":"admin"}})", {}, {}};
                return FakeResponse{401, "{}", {}, {}};
            });
            long long now = 0;
            CernereAuth auth(io, {false, "http://127.0.0.1:" + std::to_string(server.port()), false}, [&] { return now; });
            auto user = verify(auth, io, "good");
            check(user && user->id == "u1" && user->role == "admin", "verified");
            check(server.seen.size() == 1 && server.seen[0] == "POST /api/auth/verify", "one request");
            check(server.bodies[0] == R"({"token":"good"})", "token in the body as before");
            verify(auth, io, "good");
            check(server.seen.size() == 1, "cached");
            now += 300'001;
            verify(auth, io, "good");
            check(server.seen.size() == 2, "expired cache re-verifies");
            check(!verify(auth, io, "bad"), "401 rejected");
            check(!verify(auth, io, ""), "empty token is not sent");
            check(server.seen.size() == 3, "no request for an empty token");
            std::vector<std::optional<VerifiedUser>> results;
            auth.invalidate("good");
            for (int i = 0; i < 3; ++i) auth.verify("good", [&](std::optional<VerifiedUser> u) { results.push_back(u); });
            run_until(io, [&] { return results.size() == 3; });
            check(results.size() == 3 && server.seen.size() == 4, "concurrent checks share one request");
        }},
        {"CernereAuth > unreachable or slow Cernere rejects instead of hanging", [] {
            boost::asio::io_context io;
            boost::asio::ip::tcp::acceptor closed(io, {boost::asio::ip::make_address("127.0.0.1"), 0});
            const auto port = closed.local_endpoint().port();
            closed.close();
            CernereAuth auth(io, {false, "http://127.0.0.1:" + std::to_string(port), false}, [] { return 0LL; });
            check(!verify(auth, io, "t"), "refused connection");
        }},
        {"http client > response body budget, deadline and redirects", [] {
            boost::asio::io_context io;
            FakeHttpServer server(io, [](const FakeHttpServer::Request& r) {
                if (r.target() == "/big") return FakeResponse{200, std::string(1025, 'x'), {}, {}};
                if (r.target() == "/exact") return FakeResponse{200, std::string(1024, 'x'), {}, {}};
                if (r.target() == "/slow") return FakeResponse{200, "late", {}, std::chrono::milliseconds(1500)};
                return FakeResponse{302, "", {{"location", "http://example.invalid/"}}, {}};
            });
            const auto base = "http://127.0.0.1:" + std::to_string(server.port());
            auto fetch = [&](const std::string& path, std::size_t limit, std::chrono::milliseconds timeout) {
                std::optional<HttpResult> result;
                HttpRequest request;
                request.url = base + path;
                request.body_limit = limit;
                request.timeout = timeout;
                http_request(io, request, [&](HttpResult r) { result = std::move(r); });
                run_until(io, [&] { return result.has_value(); });
                return *result;
            };
            auto exact = fetch("/exact", 1024, std::chrono::seconds(5));
            check(std::get<HttpResponse>(exact).body.size() == 1024, "exactly at the limit");
            auto big = fetch("/big", 1024, std::chrono::seconds(5));
            check(std::get<HttpError>(big).kind == HttpFailure::too_large, "limit + 1 rejected");
            auto slow = fetch("/slow", 1024, std::chrono::milliseconds(200));
            check(std::get<HttpError>(slow).kind == HttpFailure::timeout, "deadline");
            auto redirect = fetch("/r", 1024, std::chrono::seconds(5));
            check(std::get<HttpResponse>(redirect).status == 302, "redirects are not followed");
            check(!parse_url("ftp://x") && !parse_url("http://a@b/") && !parse_url("http://h:0/") && parse_url("https://[::1]:8443/p?q")->host == "::1", "URL parsing");
        }},
    });
}
