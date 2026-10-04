#include "poll_draft.hpp"
#include <algorithm>
#include <cctype>
#include <set>

namespace odeum::presenter {
namespace {
std::string_view trim(std::string_view text) {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) text.remove_prefix(1);
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) text.remove_suffix(1);
    return text;
}
std::size_t characters(std::string_view text) {
    try { return utf8_length(text); }
    catch (const ProtocolError&) { throw PollDraftError("invalid_text", "The poll text is not valid UTF-8"); }
}
}

std::vector<std::string> PollDraft::split_choices(std::string_view text) {
    std::vector<std::string> result;
    while (!text.empty()) {
        const auto end = text.find('\n');
        const auto line = trim(text.substr(0, end));
        if (!line.empty()) result.emplace_back(line);
        if (end == std::string_view::npos) break;
        text.remove_prefix(end + 1);
    }
    return result;
}

Json PollDraft::build(const std::string& poll_id) const {
    const auto q = trim(question);
    if (q.empty()) throw PollDraftError("question_missing", "The poll needs a question");
    if (characters(q) > poll_question_max_chars) throw PollDraftError("question_too_long", "The question is longer than 1000 characters");
    if (choices.size() < poll_min_choices) throw PollDraftError("too_few_choices", "A poll needs at least two choices");
    if (choices.size() > poll_max_choices) throw PollDraftError("too_many_choices", "A poll has at most six choices");
    Json list = Json::array();
    std::set<std::string> seen;
    for (const auto& raw : choices) {
        const std::string choice(trim(raw));
        const auto n = characters(choice);
        if (n == 0) throw PollDraftError("too_few_choices", "A choice is empty");
        if (n > poll_choice_max_chars) throw PollDraftError("choice_too_long", "A choice is longer than 60 characters");
        // Two identical choices would split one opinion across two counters.
        if (!seen.insert(choice).second) throw PollDraftError("duplicate_choice", "Two choices are the same");
        list.push_back(choice);
    }
    Json message = {{"type", "poll.open"}, {"poll_id", poll_id}, {"question", std::string(q)}, {"choices", std::move(list)}, {"multi", multi}};
    // The relay applies the same schema; checking here keeps a rejected poll from leaving the app.
    try { serialize_message({MessageType::poll_open, message}); }
    catch (const ProtocolError&) { throw PollDraftError("invalid_text", "The poll does not fit the relay's message limits"); }
    return message;
}

Json PollControl::open(const PollDraft& draft, const std::string& poll_id) {
    if (open_) throw PollDraftError("poll_already_open", "Close the current poll first");
    auto message = draft.build(poll_id);
    open_ = poll_id;
    return message;
}

Json PollControl::close() {
    if (!open_) throw PollDraftError("no_open_poll", "No poll is open");
    Json message = {{"type", "poll.close"}, {"poll_id", *open_}};
    open_.reset();
    return message;
}
}
