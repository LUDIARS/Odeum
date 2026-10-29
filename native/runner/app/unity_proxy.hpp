#pragma once
// /api/unity/:appId/* — proxy to the Unity bridge of a registered app (custos/src/routes/unity-routes.ts).
// An unreachable bridge (domain reload) is 200 { bridge: "down" }, a 504 is "busy". State-changing
// calls are refused while another connection plays the app.
#include "../adapters/bridge_client.hpp"
#include "../core/control_lease.hpp"
#include "../core/registry.hpp"
#include "http_types.hpp"

namespace odeum::runner {
class UnityProxy {
public:
    using ClientFactory = std::function<std::shared_ptr<UnityBridgeClient>(const BridgeTarget&)>;
    UnityProxy(const AppsRegistry& registry, ControlLease& lease, ClientFactory factory);
    // segments after /api/unity: { appId, action }. Returns false when no route matches.
    bool handle(const ServerRequest& request, const std::string& app_id, const std::string& action, Reply reply);
private:
    const AppsRegistry& registry_;
    ControlLease& lease_;
    ClientFactory factory_;
    using Call = std::function<void(UnityBridgeClient&, std::function<void(UnityResult)>)>;
    void proxy(const std::string& app_id, Call call, Reply reply);
};
// JavaScript Number(text) for query values ("" -> 0, junk -> NaN).
double js_to_number(std::string_view text);
}
