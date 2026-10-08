#pragma once
#include <odeum/message.hpp>
#include <deque>

namespace odeum::presenter {
struct Submission {
    std::string text, category, name;
    bool show_on_screen = false;
};
// Session-local inbox, intentionally separate from drawable overlay state.
class SubmissionInbox {
public:
    void accept(const Message& message) {
        if (message.type != MessageType::submission || !message.body.contains("from")) return;
        const auto& b = message.body;
        entries_.push_front({b.at("text").get<std::string>(), b.at("category").get<std::string>(),
            b.at("from").at("name").get<std::string>(), b.at("show_on_screen").get<bool>()});
        if (entries_.size() > 100) entries_.pop_back();
        selected_ = 0;
    }
    void reset() { entries_.clear(); selected_ = 0; }
    void next() { if (!entries_.empty()) selected_ = (selected_ + 1) % entries_.size(); }
    void previous() { if (!entries_.empty()) selected_ = (selected_ + entries_.size() - 1) % entries_.size(); }
    const Submission* selected() const { return entries_.empty() ? nullptr : &entries_[selected_]; }
    std::size_t index() const { return selected_; }
    std::size_t size() const { return entries_.size(); }
private:
    std::deque<Submission> entries_;
    std::size_t selected_ = 0;
};
}
