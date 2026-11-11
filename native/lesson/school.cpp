#include "school.hpp"
#include "invitations.hpp"
#include <openssl/rand.h>
#include <array>

namespace odeum::lesson {
namespace {
template<std::size_t N> std::array<unsigned char, N> random_bytes() {
    std::array<unsigned char, N> bytes{};
    if (RAND_bytes(bytes.data(), static_cast<int>(bytes.size())) != 1) throw std::runtime_error("Random source unavailable");
    return bytes;
}
}
std::string random_join_code() {
    constexpr std::string_view alphabet = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";
    std::string code;
    for (const auto byte : random_bytes<10>()) code.push_back(alphabet[byte & 31]);
    return code;
}
std::string random_token() {
    constexpr std::string_view digits = "0123456789abcdef";
    std::string token;
    for (const auto byte : random_bytes<16>()) { token.push_back(digits[byte >> 4]); token.push_back(digits[byte & 15]); }
    return token;
}

School::School(std::vector<Plot> catalog, std::shared_ptr<const LessonModel> model, std::size_t max_students)
    : catalog_(std::move(catalog)), model_(std::move(model)), max_students_(max_students) {}

const Plot* School::plot(std::string_view id) const {
    for (const auto& plot : catalog_) if (plot.id == id) return &plot;
    return nullptr;
}
School::Opened School::open(const std::string& plot_id) {
    const auto found = plot(plot_id);
    if (!found) throw ProtocolError("unknown_plot", "Unknown course");
    if (classes_.size() >= max_classes) throw ProtocolError("capacity", "Too many classes are running");
    auto code = random_join_code();
    while (codes_.contains(code)) code = random_join_code();
    const auto id = "c" + std::to_string(++next_);
    classes_.emplace(id, Running{code, std::make_unique<Classroom>(*found, model_, random_token, max_students_)});
    codes_.emplace(code, id);
    return {id, code};
}
void School::close(const std::string& class_id) {
    const auto found = classes_.find(class_id);
    if (found == classes_.end()) throw ProtocolError("unknown_class", "Unknown class");
    codes_.erase(found->second.code);
    classes_.erase(found);
}
Classroom& School::classroom(const std::string& class_id) {
    const auto found = classes_.find(class_id);
    if (found == classes_.end()) throw ProtocolError("unknown_class", "Unknown class");
    return *found->second.classroom;
}
std::string School::resolve(std::string_view code, Millis now) {
    while (!failures_.empty() && failures_.front() <= now - 60'000) failures_.pop_front();
    if (failures_.size() >= max_failures_per_minute) throw ProtocolError("rate_limited", "Too many wrong codes; wait a minute");
    if (const auto normalised = relay::normalize_join_code(code)) {
        if (const auto found = codes_.find(*normalised); found != codes_.end()) return found->second;
    }
    failures_.push_back(now);
    throw ProtocolError("class_not_found", "No class with this code");
}
Json School::catalog() const {
    Json plots = Json::array(), classes = Json::array();
    for (const auto& plot : catalog_) plots.push_back({{"id", plot.id}, {"title", plot.title}, {"subtitle", plot.subtitle},
        {"minutes", plot.minutes}, {"engine", plot.engine}});
    for (const auto& [id, running] : classes_) classes.push_back({{"id", id}, {"code", running.code},
        {"plot", running.classroom->plot().id}, {"title", running.classroom->plot().title}});
    return {{"type", "catalog"}, {"plots", std::move(plots)}, {"classes", std::move(classes)}, {"model", model_->name()}};
}
std::vector<std::pair<std::string, Outbox>> School::tick(Millis now) {
    std::vector<std::pair<std::string, Outbox>> out;
    for (auto& [id, running] : classes_) if (auto messages = running.classroom->tick(now); !messages.empty()) out.emplace_back(id, std::move(messages));
    return out;
}
}
