// custos/tests/comment-message.test.ts, control-lease.test.ts, audience-room.test.ts, stream-prefs.test.ts
// + golden (message_cases, control_lease_cases, audience_room, stream_prefs_cases).
#include "runner_check.hpp"
#include <core/audience.hpp>
#include <core/client_message.hpp>
#include <core/control_lease.hpp>
#include <core/stream_prefs.hpp>

using namespace odeum::runner;
using namespace runner_test;

namespace {
Json opt(const std::optional<std::string>& v) { return v ? Json(*v) : Json(nullptr); }
}

int main(int argc, char** argv) {
    return run_all(argc, argv, "runner.messages", {
        {"comment-message > golden normalizeCommentText / canPostComment / isCommentMessage", [] {
            const auto g = fixture("message_cases.json");
            for (const auto& c : g["comments"]) check_json(opt(normalize_comment_text(c["text"].get<std::string>())), c["output"], "comment " + c["text"].dump());
            for (const auto& c : g["canPost"]) {
                std::optional<long long> last;
                if (!c["last"].is_null()) last = c["last"].get<long long>();
                check(can_post_comment(last, c["now"].get<long long>()) == c["output"].get<bool>(), "canPost " + c.dump());
            }
            for (const auto& c : g["commentMessages"]) check(parse_comment_message(c["input"]).has_value() == c["output"].get<bool>(), "comment message " + c.dump());
        }},
        {"handler > golden labelToId", [] {
            for (const auto& c : Json(fixture("message_cases.json")["labels"])) check(label_to_id(c["label"].get<std::string>()) == c["output"].get<std::string>(), "label " + c.dump());
        }},
        {"pointer-message > golden isPointerMessage", [] {
            for (const auto& c : Json(fixture("message_cases.json")["pointers"])) check(parse_pointer_message(c["input"]).has_value() == c["output"].get<bool>(), "pointer " + c.dump());
        }},
        {"audience-room > golden AudienceSubmission", [] {
            for (const auto& c : Json(fixture("message_cases.json")["audience"])) {
                auto parsed = parse_audience_submission(c["input"]);
                check(parsed.has_value() == c["ok"].get<bool>(), "audience acceptance " + c["input"].dump());
                if (!parsed) continue;
                Json data{{"type", "audience-send"}, {"appId", parsed->app_id}, {"kind", parsed->kind}};
                if (parsed->kind == "message") data["text"] = parsed->text; else data["stamp"] = parsed->stamp;
                check_json(data, c["output"], "audience data");
            }
        }},
        {"audience-room > golden bounded history and sender truncation", [] {
            long long t = 1000;
            int n = 0;
            AudienceRoom room([&] { return t++; }, [&] { return "id-" + std::to_string(++n); });
            for (int i = 0; i < 203; ++i) room.append({"a", "message", "m" + std::to_string(i), {}}, "観戦者 ab12");
            room.append({"b", "stamp", {}, "party"}, std::string(70, 'x'));
            const auto g = fixture("audience_room.json");
            check_json(room.history_json("a"), g["a"], "history a");
            check_json(room.history_json("b"), g["b"], "history b");
            check_json(room.history_json("c"), g["c"], "history c");
        }},
        {"control-lease > golden operation sequence", [] {
            ControlLease lease;
            Json events = Json::array();
            lease.subscribe([&](const std::string& app, const std::optional<std::string>& holder) { events.push_back(Json{{"appId", app}, {"holder", opt(holder)}}); });
            for (const auto& step : fixture("control_lease_cases.json")) {
                const auto op = step["op"].get<std::string>();
                const auto& args = step["args"];
                events = Json::array();
                Json result = nullptr;
                if (op == "acquire") result = lease.acquire(args[0], args[1]);
                if (op == "release") lease.release(args[0], args[1]);
                if (op == "releaseAll") lease.release_all(args[0]);
                if (op == "roleOf") result = lease.role_of(args[0], args[1]);
                if (op == "mayOperate") result = lease.may_operate(args[0], args[1].get<std::string>());
                if (op == "mayOperateNone") result = lease.may_operate(args[0], std::nullopt);
                check_json(result, step["result"], "result of " + step.dump());
                check_json(events, step["events"], "events of " + step.dump());
            }
        }},
        {"stream-prefs > golden patch validation and change events", [] {
            RuntimeStreamPrefs prefs;
            int changes = 0;
            prefs.on_change([&](const StreamPrefs&) { ++changes; });
            for (const auto& c : fixture("stream_prefs_cases.json")) {
                auto parsed = parse_stream_prefs_patch(c["patch"]);
                const bool ok = std::holds_alternative<StreamPrefsPatch>(parsed);
                check(ok == c["ok"].get<bool>(), "acceptance " + c["patch"].dump());
                if (!ok) continue;
                const int before = changes;
                prefs.apply(std::get<StreamPrefsPatch>(parsed));
                check(prefs.to_json().dump() == c["state"].dump(), "state " + prefs.to_json().dump() + " vs " + c["state"].dump());
                check((changes != before) == c["changed"].get<bool>(), "change event " + c["patch"].dump());
            }
        }},
        {"stream-prefs > reset emits only when something was set", [] {
            RuntimeStreamPrefs prefs;
            int changes = 0;
            prefs.on_change([&](const StreamPrefs&) { ++changes; });
            prefs.reset();
            check(changes == 0, "empty reset");
            StreamPrefsPatch patch;
            patch.max_width = 320LL;
            prefs.apply(patch);
            prefs.reset();
            check(changes == 2 && !prefs.get().max_width, "reset clears");
        }},
        {"boundary > WebSocket message budget", [] {
            std::string at_limit = "\"" + std::string(ws_message_limits.bytes - 2, 'a') + "\"";
            check(try_parse_json(at_limit, ws_message_limits).has_value(), "exactly at the byte limit");
            check(!try_parse_json(at_limit + " ", ws_message_limits).has_value(), "limit + 1");
            std::string deep = std::string(9, '[') + std::string(9, ']');
            check(!try_parse_json(deep, ws_message_limits).has_value(), "depth 9 > 8");
            check(try_parse_json(std::string(8, '[') + std::string(8, ']'), ws_message_limits).has_value(), "depth 8");
            std::string many = "[";
            for (int i = 0; i < 1100; ++i) many += i ? ",0" : "0";
            many += "]";
            check(!try_parse_json(many, ws_message_limits).has_value(), "element count");
            check(!try_parse_json("{\"type\":", ws_message_limits).has_value(), "truncated JSON");
            check(!try_parse_json("\"\xff\"", ws_message_limits).has_value(), "invalid UTF-8");
        }},
    });
}
