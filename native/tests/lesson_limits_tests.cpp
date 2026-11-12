// Boundary tests for odeum-lesson's inputs (job cpp-lorica-20261111): WebSocket messages, tool POST
// bodies and plot files. Each limit is tried at exactly its value, one over, malformed, and with a
// huge declared or nested size.
#include "lesson_fixture.hpp"
#include "assets.hpp"
#include "input_limits.hpp"
#include <filesystem>
#include <fstream>
#include <unistd.h>
using namespace odeum;
using namespace odeum::lesson;

namespace {
// {"type":"answer","choices":[0,0,...]}: 1 object + 2 keys + 1 string + 1 array + n numbers.
std::string answer_with(std::size_t numbers) {
    std::string wire = R"({"type":"answer","choices":[)";
    for (std::size_t i = 0; i < numbers; ++i) wire += i ? ",0" : "0";
    return wire + "]}";
}
// An object nested `depth` deep: {"a":{"a":...{}}}.
std::string nested(std::size_t depth) {
    std::string wire;
    for (std::size_t i = 1; i < depth; ++i) wire += R"({"a":)";
    wire += "{}";
    for (std::size_t i = 1; i < depth; ++i) wire += "}";
    return wire;
}
// {"k":"aaa..."} of exactly `size` bytes.
std::string object_of(std::size_t size) { return R"({"k":")" + std::string(size - 8, 'a') + "\"}"; }
std::filesystem::path write_temp(const std::string& name, const std::string& text) {
    const auto path = std::filesystem::temp_directory_path() / (name + "-" + std::to_string(::getpid()) + ".json");
    std::ofstream(path, std::ios::binary) << text;
    return path;
}
std::string sample_course() {
    const auto asset = find_asset("/courses/action-game.plot.json");
    check(asset != nullptr, "Sample course asset");
    return std::string(asset->body);
}
// An array of `values` values in all: [0,0,...] (1 array + values - 1 numbers).
std::string array_of_values(std::size_t values) {
    std::string wire = "[";
    for (std::size_t i = 0; i + 1 < values; ++i) wire += i ? ",0" : "0";
    return wire + "]";
}
// A copy of the sample course with a vocabulary list padded to `terms` entries.
std::string plot_with_terms(std::size_t terms) {
    Json document = Json::parse(sample_course());
    auto& vocabulary = document["vocabulary"];
    while (vocabulary.size() < terms) vocabulary.push_back({{"term", "t" + std::to_string(vocabulary.size())}, {"meaning", "m"}});
    return document.dump();
}
}

int main() { return run([] {
    // WebSocket messages: 16 KiB, 1024 values, depth 8.
    // 1 object + 2 keys + 1 string + 1 array = 5, so elements - 5 numbers make exactly 1024 values.
    check(parse_client_message(answer_with(message_limits.elements - 5)).at("choices").size() == message_limits.elements - 5,
        "A message with exactly 1024 values is accepted");
    rejects([] { parse_client_message(answer_with(message_limits.elements - 4)); }, "invalid_message");
    check(parse_client_message(nested(message_limits.depth)).is_object(), "Depth 8 is accepted");
    rejects([] { parse_client_message(nested(message_limits.depth + 1)); }, "invalid_message");
    check(parse_client_message(object_of(message_limits.bytes)).is_object(), "A 16 KiB message is accepted");
    rejects([] { parse_client_message(object_of(message_limits.bytes + 1)); }, "invalid_message");
    rejects([] { parse_client_message(R"({"type":"question","text":)"); }, "invalid_message");
    rejects([] { parse_client_message("[1,2]"); }, "invalid_message");
    rejects([] { parse_client_message("{\"type\":\"\xff\"}"); }, "invalid_message");
    // 16 KiB of opening brackets (the deepest a frame can declare) is refused by depth at the 9th.
    rejects([] { parse_client_message(std::string(message_limits.bytes, '[')); }, "invalid_message");
    // 1e9 as a count of choices is only a number; the classroom checks it against the plot.
    check(parse_client_message(R"({"type":"answer","choices":[1000000000]})").at("choices")[0] == 1000000000, "Numbers are values, not sizes");

    // Tool bodies: 4 KiB, 64 values, depth 4.
    const auto pair = parse_tool_body(R"({"link":"ABCDEFGH","tool":"ergo-studio"})");
    check(pair.at("link") == "ABCDEFGH", "A pairing body is accepted");
    check(parse_tool_body(R"({"assignment_id":"a1","step":2,"done":true})").at("step") == 2, "A progress body is accepted");
    check(parse_tool_body(array_of_values(tool_body_limits.elements)).size() == tool_body_limits.elements - 1, "Exactly 64 values are accepted");
    rejects([] { parse_tool_body(array_of_values(tool_body_limits.elements + 1)); }, "invalid_request");
    check(parse_tool_body(nested(tool_body_limits.depth)).is_object(), "Depth 4 is accepted");
    rejects([] { parse_tool_body(nested(tool_body_limits.depth + 1)); }, "invalid_request");
    check(parse_tool_body(object_of(tool_body_limits.bytes)).is_object(), "A 4 KiB body is accepted");
    rejects([] { parse_tool_body(object_of(tool_body_limits.bytes + 1)); }, "invalid_request");
    rejects([] { parse_tool_body(R"({"link":)"); }, "invalid_request");
    rejects([] { parse_tool_body(""); }, "invalid_request");
    rejects([] { parse_tool_body("{\"link\":\"\xc0\xaf\"}"); }, "invalid_message");
    check(max_request_body_bytes == tool_body_limits.bytes, "The HTTP body limit and the JSON budget agree");

    // Plot files: 1 MiB (read in chunks), 16,384 values, depth 16.
    const auto sample = sample_course();
    check(parse_plot(parse_plot_text(sample)).id == "action-game", "The built-in course fits the plot limits");
    rejects([] { parse_plot_text(nested(plot_file_limits.depth + 1)); }, "invalid_plot");
    check(parse_plot_text(nested(plot_file_limits.depth)).is_object(), "Depth 16 is accepted");
    check(parse_plot_text(array_of_values(plot_file_limits.elements)).size() == plot_file_limits.elements - 1, "Exactly 16,384 values are accepted");
    rejects([] { parse_plot_text(array_of_values(plot_file_limits.elements + 1)); }, "invalid_plot");
    rejects([] { parse_plot_text(R"({"id":)"); }, "invalid_plot");

    const auto exact = write_temp("lesson-plot-exact", object_of(plot_file_limits.bytes));
    const auto over = write_temp("lesson-plot-over", object_of(plot_file_limits.bytes + 1));
    const auto broken = write_temp("lesson-plot-broken", "{\"id\":");
    const auto padded = write_temp("lesson-plot-padded", plot_with_terms(40));
    // Exactly 1 MiB passes the reader and the budget; it is then not a plot (no sections).
    bool not_a_plot = false;
    try { load_plot_file(exact.string()); } catch (const ProtocolError& error) { not_a_plot = error.code == "invalid_plot" && std::string(error.what()).find("too large") == std::string::npos; }
    check(not_a_plot, "A plot file of exactly 1 MiB is read and judged by its content, not refused as too large");
    bool too_large = false;
    try { load_plot_file(over.string()); } catch (const ProtocolError& error) { too_large = error.code == "invalid_plot" && std::string(error.what()).find("too large") != std::string::npos; }
    check(too_large, "A plot file of 1 MiB + 1 byte is refused as too large");
    rejects([&] { load_plot_file(broken.string()); }, "invalid_plot");
    check(load_plot_file(padded.string()).vocabulary.size() == 40, "A real plot file within the limits loads (40 terms, the plot's own list limit)");
    bool unreadable = false;
    try { load_plot_file((std::filesystem::temp_directory_path() / "odeum-lesson-no-such-plot.json").string()); }
    catch (const ProtocolError&) {} catch (const std::runtime_error&) { unreadable = true; }
    check(unreadable, "A missing plot file is an error, not an empty plot");
    for (const auto& path : {exact, over, broken, padded}) std::filesystem::remove(path);
}); }
