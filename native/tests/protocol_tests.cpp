#include "check.hpp"
#include <vector>
using namespace odeum;
int main() { return run([] {
    const std::vector<Json> examples = {
        {{"type","good"},{"count",50}}, {{"type","stamp"},{"kind","clap"}}, {{"type","comment"},{"text","日本語😀"}},
        {{"type","sdp"},{"sdp",{{"type","offer"},{"sdp","v=0\r\n"}}}},
        {{"type","candidate"},{"candidate","candidate:1 1 UDP 1 127.0.0.1 1234 typ host"},{"mid","0"}},
        {{"type","poll.open"},{"poll_id","p"},{"question","Q"},{"choices",{"a","b"}},{"multi",false}},
        {{"type","poll.close"},{"poll_id","p"}}, {{"type","poll.answer"},{"poll_id","p"},{"choices",{0,1}}},
        {{"type","poll.closed"},{"poll_id","p"},{"tally",{{"counts",{1,0}},{"answered",1}}}},
        {{"type","tally"},{"poll_id","p"},{"counts",{1,0}},{"answered",1}},
        {{"type","reaction.burst"},{"good",1},{"stamps",{{"clap",1}}}},
        {{"type","presence"},{"presenter_connected",true},{"viewer_count",2}},
        {{"type","welcome"},{"sid","s"},{"role","viewer"},{"self",{{"sub","u"},{"name","名前"}}},{"ice_servers",Json::array()}},
        error_message("unknown_type","Unknown type")
    };
    for (const auto& example : examples) {
        const auto message = parse_message(example.dump());
        check(Json::parse(serialize_message(message)) == example, "JSON round trip");
    }
    for (int value : {0,51,-1}) rejects([&] { parse_message(Json{{"type","good"},{"count",value}}.dump()); });
    rejects([] { parse_message(R"({"type":"good","count":1.1})"); });
    rejects([] { parse_message(R"({"type":"future"})"); }, "unknown_type");
    rejects([] { parse_message(std::string(16385,'x')); }, "message_too_large");
    rejects([] { parse_message(R"({"type":"poll.answer","poll_id":"p","choices":[0,0]})"); });
    rejects([] { parse_message(R"({"type":"stamp","kind":"unknown"})"); });
    std::string text; for (int i=0; i<280; ++i) text += "😀";
    parse_message(Json{{"type","comment"},{"text",text}}.dump());
    text += "a"; rejects([&] { parse_message(Json{{"type","comment"},{"text",text}}.dump()); });
    check(utf8_length("日本語😀") == 4, "Unicode scalar count");
    rejects([] { utf8_length(std::string("\xc0\xaf",2)); });
    rejects([] { utf8_length(std::string("\xed\xa0\x80",3)); });
    Json poll = examples[5]; poll["choices"] = {"a"}; rejects([&] { parse_message(poll.dump()); });
    poll["choices"] = {"a","b","c","d","e","f"}; parse_message(poll.dump());
    poll["choices"].push_back("g"); rejects([&] { parse_message(poll.dump()); });
    poll["choices"] = {std::string(61,'a'),"b"}; rejects([&] { parse_message(poll.dump()); });
}); }
