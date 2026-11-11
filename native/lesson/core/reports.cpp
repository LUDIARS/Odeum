#include "reports.hpp"
#include <algorithm>

namespace odeum::lesson {
namespace {
const char* state_name(TroubleReports::State state) {
    return state == TroubleReports::State::reported ? "reported" : state == TroubleReports::State::acknowledged ? "acknowledged" : "replied";
}
}
const TroubleReports::Report& TroubleReports::find(const std::string& id) const {
    const auto found = reports_.find(id);
    if (found == reports_.end()) throw ProtocolError("unknown_report", "Unknown report");
    return found->second;
}
TroubleReports::Report& TroubleReports::find(const std::string& id) {
    return const_cast<Report&>(static_cast<const TroubleReports&>(*this).find(id));
}

std::string TroubleReports::add(const std::string& student, const std::string& text) {
    if (reports_.size() >= max_reports) throw ProtocolError("too_many_reports", "Report limit reached");
    Report report{"r" + std::to_string(++next_), student, text};
    const auto id = report.id;
    order_.push_back(id);
    reports_.emplace(id, std::move(report));
    return id;
}
void TroubleReports::acknowledge(const std::string& id) {
    auto& report = find(id);
    if (report.state == State::reported) report.state = State::acknowledged;
}
void TroubleReports::reply(const std::string& id, const std::string& text) {
    auto& report = find(id);
    report.state = State::replied;
    report.reply = text;
}
Json TroubleReports::status(const std::string& id) const {
    const auto& report = find(id);
    return {{"id", report.id}, {"text", report.text}, {"state", state_name(report.state)}, {"reply", report.reply}};
}
Json TroubleReports::mine(const std::string& student) const {
    Json out = Json::array();
    for (const auto& id : order_) if (reports_.at(id).student == student) out.push_back(status(id));
    return out;
}
Json TroubleReports::console_list() const {
    Json out = Json::array();
    for (const auto& id : order_) {
        auto entry = status(id);
        entry["student"] = reports_.at(id).student;
        out.push_back(std::move(entry));
    }
    return out;
}
std::size_t TroubleReports::open() const {
    return static_cast<std::size_t>(std::count_if(reports_.begin(), reports_.end(), [](const auto& entry) { return entry.second.state == State::reported; }));
}
std::vector<std::string> TroubleReports::take_unacknowledged(const std::string& student) {
    std::vector<std::string> out;
    for (const auto& id : order_) {
        auto& report = reports_.at(id);
        if (report.student != student || report.state != State::reported || report.told_at_end) continue;
        report.told_at_end = true;
        out.push_back(id);
    }
    return out;
}
void TroubleReports::withdraw(const std::string& student) {
    std::erase_if(order_, [&](const std::string& id) { return reports_.at(id).student == student; });
    std::erase_if(reports_, [&](const auto& entry) { return entry.second.student == student; });
}
}
