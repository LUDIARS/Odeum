#pragma once
// What the browser may see of an app (custos/src/routes/apps-routes.ts `redact`): no command,
// working directory, environment, bridge host or port.
#include "app_config.hpp"

namespace odeum::runner {
Json redact(const AppConfig& app);
}
