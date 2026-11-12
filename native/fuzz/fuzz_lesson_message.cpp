// libFuzzer entry: one odeum-lesson WebSocket message (lesson/input_limits.cpp), then the classroom
// that receives it, both as a student and as the operator console (lesson/core/classroom.cpp).
// Each input gets a fresh classroom of the built-in course with one joined student and a started
// lesson, so a crash reproduces from the input alone.
#include "catalog.hpp"
#include "classroom.hpp"
#include "input_limits.hpp"
#include <cstddef>
#include <cstdint>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    using namespace odeum::lesson;
    static const auto plot = builtin_plots().front();
    static const std::shared_ptr<const LessonModel> model(make_provisional_model());
    odeum::Json message;
    try { message = parse_client_message({reinterpret_cast<const char*>(data), size}); }
    catch (const odeum::ProtocolError&) { return 0; }
    int tokens = 0;
    Classroom classroom(plot, model, [&tokens] { return "token" + std::to_string(++tokens); });
    const auto student = classroom.admit("fuzz", "").student;
    classroom.connect(student, 0);
    classroom.from_console({{"type", "lesson.start"}}, 0);
    // The server answers every exception with an error message; the fuzzer looks for crashes and
    // undefined behaviour, not for refusals.
    try { classroom.from_student(student, message, 1000); } catch (const std::exception&) {}
    try { classroom.from_console(message, 2000); } catch (const std::exception&) {}
    try { classroom.tick(3000); } catch (const std::exception&) {}
    return 0;
}
