#pragma once
// HTTP routes of odeum-runner, in the order of the former Hono app (custos/src/app.ts):
// /api/health, /api/video/encoder, /api/apps (auth), /api/settings (auth), /api/unity (auth),
// /api/playlogs (auth), /api/farm (when configured), the embedded browser UI, then 404.
#include "../adapters/cernere_client.hpp"
#include "../adapters/ice_provider.hpp"
#include "../adapters/media_tools.hpp"
#include "../core/farm.hpp"
#include "../core/farm_host_auth.hpp"
#include "apps_runner.hpp"
#include "http_types.hpp"
#include "rtc_service.hpp"
#include "screenshot_streamer.hpp"
#include "unity_proxy.hpp"

namespace odeum::runner {
struct FarmRuntime {
    std::unique_ptr<FarmStore> store;
    std::unique_ptr<DevicePool> pool;
    std::unique_ptr<FarmHostAuth> host_auth;
};

struct RouterDeps {
    AppsRegistry& registry;
    AppsRunner& runner;
    RuntimeStreamPrefs& prefs;
    CernereAuth& auth;
    UnityProxy& unity;
    MediaTools& media;
    ScreenshotSource& screenshots;
    IceServerProvider& ice;
    RtcService* rtc;     // null: built without WebRTC
    FarmRuntime* farm;   // null: CUSTOS_FARM_CONFIG unset
    std::string playlog_dir;
    std::string gaze_assets_dir;
    bool encoder_env_set;  // CUSTOS_VIDEO_ENCODER present (the former `forced` flag)
};

class ApiRouter final : public RequestHandler {
public:
    explicit ApiRouter(RouterDeps deps) : deps_(std::move(deps)) {}
    void handle(const ServerRequest& request, Reply reply) override;
private:
    RouterDeps deps_;
    void authenticated(const ServerRequest& request, Reply reply, std::function<void(const VerifiedUser&)> next);
    bool apps(const ServerRequest& request, const std::vector<std::string>& s, Reply reply);
    bool settings(const ServerRequest& request, const std::vector<std::string>& s, Reply reply);
    bool playlogs(const ServerRequest& request, const std::vector<std::string>& s, Reply reply);
    bool farm(const ServerRequest& request, const std::vector<std::string>& s, Reply reply);
    void serve_static(const ServerRequest& request, Reply reply);
};
}
