#pragma once
#include "types.hpp"
#include <map>
#include <optional>

namespace odeum::lesson {
// Who is speaking right now, from each student's own microphone level. Each student's audio
// reaches the classroom on that student's connection, so telling voices apart needs no model here.
class VoiceDesk {
public:
    static constexpr int speaking_level = 12;        // 0..100; above this the student counts as speaking
    static constexpr Millis hangover_ms = 700;        // a pause shorter than this is still the same utterance
    static constexpr Millis stale_ms = 3'000;         // no level report for this long -> microphone treated as off
    // Returns true when the student's speaking state changed.
    bool level(const std::string& student, int level, Millis now);
    void leave(const std::string& student);
    bool speaking(const std::string& student, Millis now) const;
    // Students speaking now, in the order they started (the first is the one the lecturer attends to).
    std::vector<std::string> speakers(Millis now) const;
    // Students whose speaking state timed out since the last call.
    std::vector<std::string> settle(Millis now);
private:
    struct Channel { Millis last_loud = -1, last_report = -1, started = -1; bool speaking = false; };
    std::map<std::string, Channel> channels_;
    static bool active(const Channel& channel, Millis now);
};

// For a room with one shared microphone: an external diarizer labels each utterance with an
// opaque speaker label, and this registry binds labels to students. A student proves which
// label is theirs by saying the three-digit number shown only on their own device.
class SpeakerRegistry {
public:
    static constexpr Millis enrollment_ms = 60'000;
    struct Heard { std::optional<std::string> student; bool enrolled_now = false; };
    // Returns the number the student must say aloud.
    std::string begin_enrollment(const std::string& student, Millis now);
    Heard heard(const std::string& speaker_label, std::string_view transcript, Millis now);
    void forget(const std::string& student);
    std::optional<std::string> student_of(const std::string& speaker_label) const;
private:
    struct Pending { std::string code; Millis expires; };
    std::map<std::string, Pending> pending_;        // student -> code
    std::map<std::string, std::string> labels_;     // speaker label -> student
    unsigned next_code_ = 0;
};
}
