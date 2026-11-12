#pragma once
// Named test cases for the odeum-runner suites. Case names follow the former vitest names in
// custos/tests so the correspondence table can be checked by name.
#include <core/json.hpp>
#include <fstream>
#include <functional>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace runner_test {
struct Case {
    std::string name;
    std::function<void()> body;
};

inline void check(bool condition, const std::string& what) {
    if (!condition) throw std::runtime_error(what);
}

template<class F> void throws(F action, const std::string& what) {
    try { action(); } catch (const std::exception&) { return; }
    throw std::runtime_error("expected an exception: " + what);
}

// Fixture directory: the first command-line argument.
inline std::string& fixture_dir() {
    static std::string dir;
    return dir;
}

inline odeum::runner::Json fixture(const std::string& name) {
    std::ifstream in(fixture_dir() + "/" + name, std::ios::binary);
    if (!in) throw std::runtime_error("missing fixture " + name);
    std::stringstream text;
    text << in.rdbuf();
    return odeum::runner::Json::parse(text.str());
}

// Order-insensitive comparison of JSON values (golden files come from JSON.stringify).
inline bool same_json(const odeum::runner::Json& a, const odeum::runner::Json& b) {
    return nlohmann::json::parse(a.dump()) == nlohmann::json::parse(b.dump());
}

inline void check_json(const odeum::runner::Json& actual, const odeum::runner::Json& expected, const std::string& what) {
    if (!same_json(actual, expected)) throw std::runtime_error(what + "\n  actual:   " + actual.dump() + "\n  expected: " + expected.dump());
}

inline int run_all(int argc, char** argv, const char* suite, const std::vector<Case>& cases) {
    if (argc > 1) fixture_dir() = argv[1];
    int failed = 0;
    for (const auto& c : cases) {
        try {
            c.body();
            std::cout << "ok   " << suite << " > " << c.name << '\n';
        } catch (const std::exception& error) {
            ++failed;
            std::cerr << "FAIL " << suite << " > " << c.name << ": " << error.what() << '\n';
        }
    }
    std::cout << suite << ": " << (cases.size() - static_cast<std::size_t>(failed)) << "/" << cases.size() << " passed\n";
    return failed == 0 ? 0 : 1;
}
}
