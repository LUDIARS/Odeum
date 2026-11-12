#include "input_limits.hpp"

namespace odeum::lesson {
namespace {
// parse_json_within throws ProtocolError for a budget and Json::exception for malformed text; the
// callers answer one code for both, so a client cannot tell which check it tripped.
template<class Parse> Json refuse_as(std::string_view code, std::string_view what, Parse parse) {
    try { return parse(); }
    catch (const ProtocolError&) { throw ProtocolError(std::string(code), std::string(what)); }
    catch (const Json::exception&) { throw ProtocolError(std::string(code), std::string(what)); }
}
}
Json parse_client_message(std::string_view wire) {
    utf8_length(wire);
    auto message = refuse_as("invalid_message", "Message rejected", [&] { return parse_json_within(wire, message_limits); });
    if (!message.is_object()) throw ProtocolError("invalid_message", "Object required");
    return message;
}
Json parse_tool_body(std::string_view body) {
    utf8_length(body);  // malformed UTF-8 stays ProtocolError("invalid_message"), as before the limits
    return refuse_as("invalid_request", "Invalid JSON body", [&] { return parse_json_within(body, tool_body_limits); });
}
Json parse_plot_text(std::string_view text) {
    return refuse_as("invalid_plot", "Plot is not valid JSON or exceeds its limits", [&] { return parse_json_within(text, plot_file_limits); });
}
}
