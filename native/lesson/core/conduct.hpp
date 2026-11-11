#pragma once
#include "types.hpp"
#include <deque>
#include <map>
#include <set>

namespace odeum::lesson {
// Decides when a student who keeps disrupting the lesson is warned, locked out, or removed.
// Locked students still see and hear the lesson but cannot post, answer, or speak to the class.
// Removal lasts for the rest of the class; only the operator can lift it.
class Conduct {
public:
    enum class Standing { good, warned, locked, ejected };
    enum class Action { none, warn, lock, eject };
    struct Verdict { Action action = Action::none; Millis locked_until = 0; std::string reason; };

    static constexpr Millis memory_ms = 10 * 60'000;      // strikes older than this are forgotten
    static constexpr Millis flood_window_ms = 10'000;
    static constexpr std::size_t flood_posts = 8;          // more than this many posts in the window is flooding
    static constexpr Millis chatter_window_ms = 60'000;
    static constexpr std::size_t chatter_limit = 3;        // off-topic remarks tolerated per window
    static constexpr int lock_points = 3, eject_points = 6;
    static constexpr std::size_t eject_locks = 3;          // the third lockout removes the student
    static constexpr Millis first_lock_ms = 60'000, max_lock_ms = 15 * 60'000;

    // Every post a student makes goes through post(); it reports flooding.
    Verdict post(const std::string& student, Millis now);
    // The model judged an utterance disruptive (severity 1 = noise, 2 = abuse).
    Verdict disruption(const std::string& student, int severity, Millis now);
    // An off-topic remark. Harmless alone; repeated ones add up.
    Verdict off_topic(const std::string& student, Millis now);
    Verdict lock(const std::string& student, Millis duration, Millis now, std::string reason);
    void unlock(const std::string& student);
    Verdict eject(const std::string& student, std::string reason);
    void readmit(const std::string& student);
    // Students whose lock ran out since the last call.
    std::vector<std::string> expire(Millis now);
    Standing standing(const std::string& student, Millis now) const;
    bool may_post(const std::string& student, Millis now) const;
    bool ejected(const std::string& student) const { return ejected_.contains(student); }
    Millis locked_until(const std::string& student) const;
    int points(const std::string& student, Millis now) const;
private:
    struct Strike { Millis at; int points; };
    struct Record {
        std::deque<Strike> strikes;
        std::deque<Millis> posts, chatter;
        Millis locked_until = 0;
        std::size_t locks = 0;
        bool warned = false, locked = false;
    };
    std::map<std::string, Record> records_;
    std::set<std::string> ejected_;
    Verdict strike(const std::string& student, int points, Millis now, const std::string& reason);
    static void forget(Record& record, Millis now);
};
std::string_view standing_name(Conduct::Standing standing);
}
