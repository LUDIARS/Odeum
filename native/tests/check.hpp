#pragma once
#include <odeum/message.hpp>
#include <iostream>
#include <functional>

inline void check(bool condition, const char* description) {
    if (!condition) throw std::runtime_error(description);
}
template<class Function> void rejects(Function action, std::string_view code = {}) {
    try { action(); } catch (const odeum::ProtocolError& error) { check(code.empty() || error.code == code, "Wrong error code"); return; }
    throw std::runtime_error("Expected rejection");
}
template<class Function> int run(Function test) {
    try { test(); return 0; } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
