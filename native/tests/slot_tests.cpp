#include "check.hpp"
#include "slots.hpp"
using namespace odeum;
using namespace odeum::relay;

int main() { return run([] {
    // Slot names: program or input1..input8, nothing else.
    check(input_index("input1") == 1u && input_index("input8") == 8u, "Input index");
    for (auto slot : {"input0", "input9", "input10", "input01", "program", "input", "INPUT1"}) check(!input_index(slot), "Not an input");
    check(valid_slot("program") && valid_slot("input3") && !valid_slot("") && !valid_slot("main"), "Valid slots");
    check(input_slot(2) == "input2", "Input slot name");

    // The relay limit (ODEUM_RELAY_MAX_INPUTS) narrows the wire range.
    require_slot("program", 1); require_slot("input4", 4);
    rejects([] { require_slot("input5", 4); }, "slot_unavailable");
    rejects([] { require_slot("input2", 1); }, "slot_unavailable");
    require_slot("input8", 8);

    // Viewer source: program first, then the lowest-numbered live input.
    check(!viewer_slot({}), "No source");
    check(viewer_slot({"input3", "input2"}) == "input2", "Lowest input");
    check(viewer_slot({"input4", "input1", "program"}) == "program", "Program wins");
    check(viewer_slot({"input8", "input7"}) == "input7", "Numeric order");

    // Presence lists every configured input and program.
    auto slots = slot_presence({"input2", "program"}, 4);
    check(slots.size() == 5 && slots.at("input1") == false && slots.at("input2") == true && slots.at("input4") == false, "Input flags");
    check(slots.at("program") == true && !slots.contains("input5"), "Program flag and configured range");
}); }
