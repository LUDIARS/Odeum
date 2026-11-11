#include "plot.hpp"
#include "text.hpp"
#include <algorithm>
#include <array>
#include <set>

namespace odeum::lesson {
namespace {
constexpr std::size_t max_sections = 40, max_points = 30, max_list = 40, max_choices = 6, max_steps = 20;
constexpr std::array kind_names{"choice", "text", "missing", "open"};

[[noreturn]] void invalid(const std::string& where) { throw ProtocolError("invalid_plot", "Invalid plot: " + where); }

std::string text(const Json& object, const char* key, std::size_t maximum, const std::string& where, bool required = true) {
    const auto found = object.find(key);
    if (found == object.end()) { if (required) invalid(where + "." + key); return {}; }
    if (!found->is_string()) invalid(where + "." + key);
    auto value = trim(found->get_ref<const std::string&>());
    std::size_t length = 0;
    try { length = utf8_length(value); } catch (const ProtocolError&) { invalid(where + "." + key); }
    if (length > maximum || (required && length == 0)) invalid(where + "." + key);
    return value;
}
std::string identifier(const Json& object, const std::string& where, std::set<std::string>& seen) {
    auto value = text(object, "id", 64, where);
    const bool ok = std::all_of(value.begin(), value.end(), [](unsigned char c) {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_'; });
    if (!ok || !seen.insert(value).second) invalid(where + ".id");
    return value;
}
int integer(const Json& object, const char* key, int low, int high, int fallback, const std::string& where) {
    const auto found = object.find(key);
    if (found == object.end()) return fallback;
    if (!found->is_number_integer()) invalid(where + "." + key);
    const auto value = found->get<std::int64_t>();
    if (value < low || value > high) invalid(where + "." + key);
    return static_cast<int>(value);
}
bool flag(const Json& object, const char* key, bool fallback, const std::string& where) {
    const auto found = object.find(key);
    if (found == object.end()) return fallback;
    if (!found->is_boolean()) invalid(where + "." + key);
    return found->get<bool>();
}
const Json& list(const Json& object, const char* key, std::size_t maximum, const std::string& where) {
    static const Json empty = Json::array();
    const auto found = object.find(key);
    if (found == object.end()) return empty;
    if (!found->is_array() || found->size() > maximum) invalid(where + "." + key);
    return *found;
}
std::vector<std::string> strings(const Json& object, const char* key, std::size_t maximum, std::size_t length, const std::string& where) {
    std::vector<std::string> out;
    for (const auto& item : list(object, key, maximum, where)) {
        if (!item.is_string()) invalid(where + "." + key);
        out.push_back(text(Json{{"v", item}}, "v", length, where + "." + key));
    }
    return out;
}
void object(const Json& value, const std::string& where) { if (!value.is_object()) invalid(where); }
// Names a variable in the running game, as the game binds it (for example "feel.hit_stop").
std::string variable_name(const Json& source, const char* key, const std::string& where) {
    auto value = text(source, key, 96, where);
    const bool ok = std::all_of(value.begin(), value.end(), [](unsigned char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-'; });
    if (!ok) invalid(where + "." + key);
    return value;
}
StepCheck step_check(const Json& source, const std::string& where) {
    object(source, where);
    static constexpr std::array ordering{"gt", "ge", "lt", "le"};
    StepCheck check{variable_name(source, "variable", where), text(source, "op", 2, where), {}};
    const bool ordered = std::find(ordering.begin(), ordering.end(), check.op) != ordering.end();
    if (!ordered && check.op != "eq" && check.op != "ne") invalid(where + ".op");
    const auto value = source.find("value");
    if (value == source.end()) invalid(where + ".value");
    const bool comparable = value->is_number() || (!ordered && (value->is_boolean() || (value->is_string() && value->get_ref<const std::string&>().size() <= 120)));
    if (!comparable) invalid(where + ".value");
    check.value = *value;
    return check;
}

std::vector<RubricPoint> rubric(const Json& source, const std::string& where) {
    std::vector<RubricPoint> out; std::set<std::string> seen;
    for (const auto& item : list(source, "points", max_list, where)) {
        object(item, where + ".points");
        RubricPoint point{identifier(item, where + ".points", seen), text(item, "label", 120, where + ".points"),
            strings(item, "keywords", max_list, 60, where + ".points")};
        if (point.keywords.empty()) invalid(where + ".points.keywords");
        out.push_back(std::move(point));
    }
    return out;
}
ActivityPlan activity(const Json& source, const std::string& where, std::set<std::string>& ids) {
    object(source, where);
    ActivityPlan plan;
    plan.id = identifier(source, where, ids);
    const auto kind = text(source, "kind", 16, where);
    const auto found = std::find(kind_names.begin(), kind_names.end(), kind);
    if (found == kind_names.end()) invalid(where + ".kind");
    plan.kind = static_cast<ActivityKind>(found - kind_names.begin());
    plan.prompt = text(source, "prompt", 400, where);
    plan.code = text(source, "code", 4000, where, false);
    plan.model_answer = text(source, "model_answer", 1000, where, false);
    plan.seconds = integer(source, "seconds", 15, 1800, 120, where);
    if (plan.kind == ActivityKind::choice) {
        plan.choices = strings(source, "choices", max_choices, 60, where);
        if (plan.choices.size() < 2) invalid(where + ".choices");
        plan.multi = flag(source, "multi", false, where);
        std::set<int> unique;
        for (const auto& index : list(source, "correct", max_choices, where)) {
            if (!index.is_number_integer()) invalid(where + ".correct");
            const auto value = index.get<std::int64_t>();
            if (value < 0 || value >= static_cast<std::int64_t>(plan.choices.size()) || !unique.insert(static_cast<int>(value)).second)
                invalid(where + ".correct");
            plan.correct.push_back(static_cast<int>(value));
        }
        if (!plan.multi && plan.correct.size() > 1) invalid(where + ".correct");
        return plan;
    }
    plan.points = rubric(source, where);
    if (plan.points.empty()) invalid(where + ".points");
    plan.min_points = static_cast<std::size_t>(integer(source, "min_points", 1, static_cast<int>(plan.points.size()), 1, where));
    // An open activity has no right answer by definition; a model answer would contradict that.
    if (plan.kind == ActivityKind::open && !plan.model_answer.empty()) invalid(where + ".model_answer");
    return plan;
}
AssignmentPlan assignment(const Json& source, const std::string& where, std::set<std::string>& ids) {
    object(source, where);
    AssignmentPlan plan;
    plan.id = identifier(source, where, ids);
    plan.title = text(source, "title", 120, where);
    plan.brief = text(source, "brief", 1000, where, false);
    plan.extra = text(source, "extra", 400, where, false);
    plan.minutes = integer(source, "minutes", 1, 240, 10, where);
    plan.stall_seconds = integer(source, "stall_seconds", 30, 3600, 180, where);
    std::set<std::string> seen;
    for (const auto& item : list(source, "steps", max_steps, where)) {
        object(item, where + ".steps");
        StepPlan step{identifier(item, where + ".steps", seen), text(item, "label", 200, where + ".steps"),
            strings(item, "hints", 5, 400, where + ".steps"), std::nullopt};
        if (const auto found = item.find("check"); found != item.end()) step.check = step_check(*found, where + ".steps[" + step.id + "].check");
        plan.steps.push_back(std::move(step));
    }
    if (plan.steps.empty()) invalid(where + ".steps");
    return plan;
}
}

Plot parse_plot(const Json& document) {
    object(document, "plot");
    Plot plot;
    std::set<std::string> plot_ids, section_ids, activity_ids, assignment_ids;
    plot.id = identifier(document, "plot", plot_ids);
    plot.title = text(document, "title", 120, "plot");
    plot.subtitle = text(document, "subtitle", 200, "plot", false);
    plot.engine = text(document, "engine", 80, "plot", false);
    plot.reference = text(document, "reference", 300, "plot", false);
    plot.audience = text(document, "audience", 200, "plot", false);
    plot.minutes = integer(document, "minutes", 5, 480, 60, "plot");
    plot.goals = strings(document, "goals", max_list, 200, "plot");
    plot.preparation = strings(document, "preparation", max_list, 300, "plot");
    for (const auto& item : list(document, "vocabulary", max_list, "plot")) {
        object(item, "plot.vocabulary");
        plot.vocabulary.push_back({text(item, "term", 60, "plot.vocabulary"), text(item, "meaning", 300, "plot.vocabulary")});
    }
    for (const auto& item : list(document, "faq", max_list, "plot")) {
        object(item, "plot.faq");
        Faq faq{strings(item, "keywords", max_list, 60, "plot.faq"), text(item, "answer", 1000, "plot.faq")};
        if (faq.keywords.empty()) invalid("plot.faq.keywords");
        plot.faq.push_back(std::move(faq));
    }
    if (const auto tool = document.find("tool"); tool != document.end()) {
        object(*tool, "plot.tool");
        const auto features = tool->find("features");
        if (features == tool->end() || !features->is_object() || features->empty() || features->size() > max_list) invalid("plot.tool.features");
        std::set<std::string> names;
        for (const auto& [feature, unused] : features->items()) {
            // "all" is how a demo addresses every feature at once, so it cannot name one.
            if (identifier(Json{{"id", feature}}, "plot.tool.features", names) == "all") invalid("plot.tool.features.all");
            plot.tool_features.emplace(feature, variable_name(*features, feature.c_str(), "plot.tool.features"));
        }
    }
    for (const auto& item : list(document, "sections", max_sections, "plot")) {
        object(item, "plot.sections");
        Section section;
        section.id = identifier(item, "plot.sections", section_ids);
        const auto where = "plot.sections[" + section.id + "]";
        section.title = text(item, "title", 120, where);
        section.minutes = integer(item, "minutes", 1, 240, 5, where);
        section.points = strings(item, "points", max_points, 600, where);
        section.keywords = strings(item, "keywords", max_list, 60, where);
        section.share_questions = flag(item, "share_questions", false, where);
        for (const auto& demo : list(item, "demos", 8, where)) {
            object(demo, where + ".demos");
            section.demos.push_back({text(demo, "feature", 64, where + ".demos"), text(demo, "note", 300, where + ".demos", false),
                flag(demo, "enabled", true, where + ".demos"),
                static_cast<std::size_t>(integer(demo, "after", 0, static_cast<int>(max_points), 0, where + ".demos"))});
            // Once the plot says which variables the demos switch, a demo nothing can switch is a typo.
            const auto& feature = section.demos.back().feature;
            if (!plot.tool_features.empty() && feature != "all" && !plot.tool_features.contains(feature)) invalid(where + ".demos.feature");
        }
        if (const auto found = item.find("activity"); found != item.end()) section.activity = activity(*found, where + ".activity", activity_ids);
        if (const auto found = item.find("assignment"); found != item.end()) section.assignment = assignment(*found, where + ".assignment", assignment_ids);
        if (const auto found = item.find("prep"); found != item.end()) {
            object(*found, where + ".prep");
            section.prep = PrepQuestion{text(*found, "question", 300, where + ".prep"), text(*found, "hint", 300, where + ".prep", false)};
        }
        plot.sections.push_back(std::move(section));
    }
    if (plot.sections.empty()) invalid("plot.sections");
    return plot;
}
std::string_view activity_kind_name(ActivityKind kind) { return kind_names.at(static_cast<std::size_t>(kind)); }
const Section* find_section(const Plot& plot, std::string_view id) {
    for (const auto& section : plot.sections) if (section.id == id) return &section;
    return nullptr;
}
const ActivityPlan* find_activity(const Plot& plot, std::string_view id) {
    for (const auto& section : plot.sections) if (section.activity && section.activity->id == id) return &*section.activity;
    return nullptr;
}
const AssignmentPlan* find_assignment(const Plot& plot, std::string_view id) {
    for (const auto& section : plot.sections) if (section.assignment && section.assignment->id == id) return &*section.assignment;
    return nullptr;
}
bool has_checked_steps(const Plot& plot) {
    for (const auto& section : plot.sections)
        if (section.assignment && std::any_of(section.assignment->steps.begin(), section.assignment->steps.end(), [](const StepPlan& step) { return step.check.has_value(); })) return true;
    return false;
}
}
