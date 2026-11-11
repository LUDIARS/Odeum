#include "server.hpp"

namespace odeum::lesson {
namespace {
constexpr int poll_ms = 2000;
}
Json Server::tool_link(const ToolDesk::Holder& holder) {
    return {{"type", "tool.link"}, {"code", school_.tools().issue(holder, monotonic_ms())}, {"seconds", ToolDesk::link_ms / 1000}};
}
Json Server::tool_pair(const Json& body) {
    const auto link = body.find("link");
    if (!body.is_object() || link == body.end() || !link->is_string()) throw ProtocolError("invalid_request", "Link code required");
    auto& tools = school_.tools();
    const auto token = tools.redeem(link->get_ref<const std::string&>(), monotonic_ms());
    const auto& holder = tools.holder(token);
    return {{"token", token}, {"role", tool_role_name(holder.role)},
        {"class", {{"title", school_.classroom(holder.class_id).plot().title}}}, {"poll_ms", poll_ms}};
}
Json Server::tool_state(std::string_view token) {
    const auto& holder = school_.tools().holder(token);
    return school_.classroom(holder.class_id).tool_state(holder.student);
}
Json Server::tool_progress(std::string_view token, const Json& body) {
    const auto holder = school_.tools().holder(token);
    if (holder.role != ToolDesk::Role::student) throw ProtocolError("forbidden_role", "Only a student's tool reports progress");
    if (!body.is_object()) throw ProtocolError("invalid_request", "Object required");
    // Only what the tool observed: the step and that it is done. A tool never un-ticks a step.
    if (body.value("done", Json()) != true) throw ProtocolError("invalid_request", "A tool reports finished steps only");
    deliver(holder.class_id, school_.classroom(holder.class_id).from_tool(holder.student, body, monotonic_ms()));
    return {{"ok", true}};
}
}
