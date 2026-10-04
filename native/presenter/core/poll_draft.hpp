#pragma once
#include <odeum/message.hpp>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace odeum::presenter {
inline constexpr std::size_t poll_min_choices = 2, poll_max_choices = 6;
inline constexpr std::size_t poll_choice_max_chars = 60, poll_question_max_chars = 1000;

struct PollDraftError : std::runtime_error {
    // question_missing | question_too_long | too_few_choices | too_many_choices | choice_too_long |
    // duplicate_choice | invalid_text | poll_already_open | no_open_poll
    std::string code;
    PollDraftError(std::string c, const std::string& message) : runtime_error(message), code(std::move(c)) {}
};

// A poll the presenter is preparing. The panel fills it from pasted text, so the fields hold
// whatever was pasted until build() checks them against the relay's limits.
struct PollDraft {
    std::string question;
    std::vector<std::string> choices;
    bool multi{};
    // One choice per line; surrounding whitespace and empty lines are dropped.
    static std::vector<std::string> split_choices(std::string_view text);
    // The poll.open message for this draft. Throws PollDraftError for the first problem found.
    Json build(const std::string& poll_id) const;
};

// The presenter's side of the single open poll the relay allows.
class PollControl {
public:
    Json open(const PollDraft& draft, const std::string& poll_id);
    Json close();
    const std::optional<std::string>& open_poll() const noexcept { return open_; }
    // The relay forgets polls when the presenter leaves, so a new connection starts with none.
    void reset() noexcept { open_.reset(); }
private:
    std::optional<std::string> open_;
};
}
