#include "voice.hpp"
#include "text.hpp"
#include <algorithm>

namespace odeum::lesson {
bool VoiceDesk::active(const Channel& channel, Millis now) {
    return channel.last_loud >= 0 && now - channel.last_loud <= hangover_ms && now - channel.last_report <= stale_ms;
}
bool VoiceDesk::level(const std::string& student, int level, Millis now) {
    auto& channel = channels_[student];
    channel.last_report = now;
    if (level >= speaking_level) channel.last_loud = now;
    const bool speaking = active(channel, now);
    if (speaking == channel.speaking) return false;
    channel.speaking = speaking;
    if (speaking) channel.started = now;
    return true;
}
void VoiceDesk::leave(const std::string& student) { channels_.erase(student); }
bool VoiceDesk::speaking(const std::string& student, Millis now) const {
    const auto found = channels_.find(student);
    return found != channels_.end() && found->second.speaking && active(found->second, now);
}
std::vector<std::string> VoiceDesk::speakers(Millis now) const {
    std::vector<std::pair<Millis, std::string>> found;
    for (const auto& [student, channel] : channels_) if (channel.speaking && active(channel, now)) found.emplace_back(channel.started, student);
    std::sort(found.begin(), found.end());
    std::vector<std::string> out;
    for (auto& entry : found) out.push_back(std::move(entry.second));
    return out;
}
std::vector<std::string> VoiceDesk::settle(Millis now) {
    std::vector<std::string> out;
    for (auto& [student, channel] : channels_) {
        if (!channel.speaking || active(channel, now)) continue;
        channel.speaking = false;
        out.push_back(student);
    }
    return out;
}

std::string SpeakerRegistry::begin_enrollment(const std::string& student, Millis now) {
    std::erase_if(pending_, [now](const auto& entry) { return entry.second.expires <= now; });
    // 100..999 in a fixed stride: codes never repeat within one enrollment window and are always three digits.
    std::string code;
    do {
        code = std::to_string(100 + (next_code_ * 37) % 900);
        ++next_code_;
    } while (std::any_of(pending_.begin(), pending_.end(), [&](const auto& entry) { return entry.second.code == code; }));
    pending_[student] = {code, now + enrollment_ms};
    return code;
}
SpeakerRegistry::Heard SpeakerRegistry::heard(const std::string& label, std::string_view transcript, Millis now) {
    std::erase_if(pending_, [now](const auto& entry) { return entry.second.expires <= now; });
    const auto folded = fold(transcript);
    for (auto it = pending_.begin(); it != pending_.end(); ++it) {
        if (!mentions(folded, it->second.code)) continue;
        const auto student = it->first;
        // One label per student and one student per label: a re-enrollment replaces the old binding.
        std::erase_if(labels_, [&](const auto& entry) { return entry.second == student; });
        labels_[label] = student;
        pending_.erase(it);
        return {student, true};
    }
    return {student_of(label), false};
}
void SpeakerRegistry::forget(const std::string& student) {
    pending_.erase(student);
    std::erase_if(labels_, [&](const auto& entry) { return entry.second == student; });
}
std::optional<std::string> SpeakerRegistry::student_of(const std::string& label) const {
    const auto found = labels_.find(label);
    return found == labels_.end() ? std::nullopt : std::optional<std::string>(found->second);
}
}
