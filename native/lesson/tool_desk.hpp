#pragma once
#include "types.hpp"
#include <deque>
#include <functional>
#include <map>
#include <string_view>

namespace odeum::lesson {
// Tools on participants' machines (Ergo Studio) and the access they hold. A participant asks
// for a short-lived link code from a page they are already signed in to, types or pastes it
// into the tool, and the tool trades it for a token. The token stands for that one participant
// in that one class and dies with either. Nothing here is written to disk.
class ToolDesk {
public:
    enum class Role { student, presenter };
    // `student` is empty for the presenter (the machine showing the demo to the class).
    struct Holder { std::string class_id, student; Role role = Role::student; };
    using Random = std::function<std::string()>;
    static constexpr Millis link_ms = 120'000;
    static constexpr std::size_t max_tokens_per_holder = 2, max_failures_per_minute = 30;

    ToolDesk(Random codes, Random tokens) : codes_(std::move(codes)), tokens_(std::move(tokens)) {}
    // A new code replaces the holder's unused one, so an overlooked code cannot be picked up later.
    std::string issue(const Holder& holder, Millis now);
    // Code -> token, once. Wrong codes are throttled across the service so a code cannot be
    // guessed by trying; throws link_not_found / rate_limited.
    // `tool` is the name the tool declares for itself (pair's "tool"); only a sanitised form is kept
    // (tool_display_name), to show the participant what linked.
    std::string redeem(std::string_view code, Millis now, std::string_view tool = {});
    const Holder& holder(std::string_view token) const;   // throws tool_unlinked
    bool linked(const std::string& class_id, const std::string& student) const;
    // What the participant's page shows about their tools: {type: "tool.linked", tools: [names], count}
    // while at least one is linked, otherwise {type: "tool.unlinked"}.
    Json link_state(const std::string& class_id, const std::string& student) const;
    // Returns whether anything was linked (or pending) for that participant.
    bool revoke_student(const std::string& class_id, const std::string& student);
    void revoke_class(const std::string& class_id);
private:
    struct Link { Holder holder; Millis expires = 0; };
    struct Grant { Holder holder; std::uint64_t order = 0; std::string tool; };
    Random codes_, tokens_;
    std::map<std::string, Link, std::less<>> links_;    // normalised code -> who asked
    std::map<std::string, Grant, std::less<>> grants_;  // token -> who it stands for
    std::deque<Millis> failures_;
    std::uint64_t next_ = 0;
};
std::string_view tool_role_name(ToolDesk::Role role);
// A tool names itself; the page shows it. Kept to ASCII letters, digits, space and . _ - + ( ), at most
// 40 characters, so a tool cannot put arbitrary text on the student's page. Empty -> a fixed label.
std::string tool_display_name(std::string_view declared);
}
