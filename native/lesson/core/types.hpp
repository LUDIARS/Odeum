#pragma once
#include <odeum/message.hpp>
#include <cstdint>
#include <string>
#include <vector>

namespace odeum::lesson {
using Millis = std::int64_t;
// A message leaving the classroom. The transport decides which sockets match the audience.
enum class Audience { everyone, student, consoles };
struct Outbound {
    Audience audience;
    std::string student;  // set only for Audience::student
    Json body;
    bool close = false;   // the transport closes the student's sockets after delivering this
};
using Outbox = std::vector<Outbound>;
inline Outbound to_all(Json body) { return {Audience::everyone, {}, std::move(body)}; }
inline Outbound to_student(std::string id, Json body) { return {Audience::student, std::move(id), std::move(body)}; }
inline Outbound to_consoles(Json body) { return {Audience::consoles, {}, std::move(body)}; }
}
