#include "check.hpp"
#include "voice.hpp"
using namespace odeum;
using namespace odeum::lesson;

int main() { return run([] {
    VoiceDesk desk;
    check(!desk.level("a", 5, 0) && !desk.speaking("a", 0), "Quiet microphone is not speaking");
    check(desk.level("a", 40, 100) && desk.speaking("a", 100), "Loud level starts speaking");
    check(!desk.level("a", 45, 200), "Still speaking: no change reported");
    check(!desk.level("a", 3, 500) && desk.speaking("a", 500), "A short pause is the same utterance");
    check(desk.level("a", 3, 100 + 200 + VoiceDesk::hangover_ms + 1), "A long pause ends it");

    // Two students talking at once are both heard, in the order they started.
    desk.level("b", 60, 2000); desk.level("a", 60, 2100); desk.level("c", 2, 2100);
    check(desk.speakers(2200) == std::vector<std::string>({"b", "a"}), "Simultaneous speakers, first speaker first");
    check(desk.speakers(2100 + VoiceDesk::hangover_ms + 1).empty(), "Silence without further reports");
    check(desk.settle(2100 + VoiceDesk::hangover_ms + 1).size() == 2 && desk.settle(9000).empty(), "Timeouts are reported once");

    // A microphone that stops reporting is treated as off even if its last level was loud.
    desk.level("d", 90, 10'000);
    check(!desk.speaking("d", 10'000 + VoiceDesk::stale_ms + 1), "Stale channel is silent");
    desk.level("d", 90, 20'000); desk.leave("d");
    check(!desk.speaking("d", 20'000) && desk.speakers(20'000).empty(), "Leaving clears the channel");

    // Shared microphone: a student claims a diarizer label by saying their number.
    SpeakerRegistry registry;
    const auto first = registry.begin_enrollment("s1", 0), second = registry.begin_enrollment("s2", 0);
    check(first.size() == 3 && second.size() == 3 && first != second, "Distinct three-digit numbers");
    check(!registry.heard("spk_0", "こんにちは", 100).student, "Unknown label is unattributed");
    auto heard = registry.heard("spk_0", "えっと、" + first + "です", 200);
    check(heard.student == "s1" && heard.enrolled_now, "Saying the number binds the label");
    heard = registry.heard("spk_0", "ヒットストップは何秒ですか", 300);
    check(heard.student == "s1" && !heard.enrolled_now, "Later speech from that label is attributed");
    check(!registry.heard("spk_1", "質問です", 400).student, "Other labels stay unknown");
    // Fullwidth digits are what a transcriber often produces for Japanese speech.
    std::string wide; for (const char digit : second) { wide += "\xef\xbc"; wide.push_back(static_cast<char>(0x90 + (digit - '0'))); }
    check(registry.heard("spk_1", wide, 500).student == "s2", "Fullwidth digits are accepted");
    check(registry.student_of("spk_1") == "s2", "Lookup by label");

    // Re-enrolling moves the student to the new label; the old label is released.
    const auto moved = registry.begin_enrollment("s1", 1000);
    registry.heard("spk_7", moved, 1100);
    check(registry.student_of("spk_7") == "s1" && !registry.student_of("spk_0"), "One label per student");
    // An unused number expires.
    const auto unused = registry.begin_enrollment("s3", 2000);
    check(!registry.heard("spk_9", unused, 2000 + SpeakerRegistry::enrollment_ms).student, "Expired number binds nothing");
    registry.forget("s2");
    check(!registry.student_of("spk_1"), "Forgetting a student releases their label");
}); }
