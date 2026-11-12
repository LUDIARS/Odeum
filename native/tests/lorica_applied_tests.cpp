// odeum.lorica_applied.baseline / .extended: this translation unit is built through odeum_harden
// (tests/lorica_applied_tests.cmake) and checks, from inside the unit, that Lorica's settings
// really reached the compiler, and that Lorica::boundary is the real one. A build that still
// compiles and passes every other test without Lorica fails here.
//
// Built with ODEUM_LORICA_TEST_EXTENDED (0 or 1) and ODEUM_LORICA_TEST_OPTIMIZED (1 in Release,
// RelWithDebInfo and MinSizeRel, the configurations where Lorica adds _FORTIFY_SOURCE).
// Exit 77 (SKIP_RETURN_CODE) only where Odeum builds without Lorica's hardening (macOS with
// ODEUM_LORICA_MACOS_UNHARDENED=ON).
//
// The rejection of the unbounded C copy and format functions (lorica_deprecate_unsafe_apis) is not
// checked here: it is seen from outside, by building probe units that call those functions and
// failing unless the build is refused with Lorica's diagnostic (odeum.lorica_applied.unsafe_api.*,
// tests/lorica_unsafe_api_probes.cmake). The probes are generated into the build tree at configure
// time, so no tracked source contains a call to the functions.

#include <cstddef>
#include <cstdio>
#include <cstring> // glibc's features.h: __USE_FORTIFY_LEVEL
#include <limits>
#include <optional>
#include <string>
#include <vector>

#include <lorica/checked_size.hpp>
#include <lorica/input_budget.hpp>

#if !defined(ODEUM_LORICA_TEST_EXTENDED) || !defined(ODEUM_LORICA_TEST_OPTIMIZED)
#error "build lorica_applied_tests.cpp through tests/lorica_applied_tests.cmake"
#endif

namespace {

std::vector<std::string> failures;
int checks = 0;

void expect(bool condition, const char* what) {
    ++checks;
    if (!condition) failures.emplace_back(what);
}

[[maybe_unused]] void note(const char* what) { std::printf("note: %s\n", what); }

// Compiler settings Lorica's LoricaHardening.cmake gives every hardened target (baseline) and the
// extended profile, as far as the unit can see them. Each #if only sets a flag; expect() reports.
void check_toolchain_settings() {
#if defined(_MSC_VER) && !defined(__clang__)
    // /GS, /sdl and /guard:cf define no macro, so a unit cannot see them; the binary check
    // (odeum.lorica_applied.binary.*, CFG marker) covers /guard:cf. Odeum does not apply
    // lorica_deprecate_unsafe_apis on MSVC (OdeumLorica.cmake: /we4996 needs the HQ build).
    // Not built in the cloud: this branch has not been compiled yet.
    note("MSVC: /GS /sdl /guard:cf are not visible from inside a unit");
#if ODEUM_LORICA_TEST_EXTENDED
#if defined(_MSVC_STL_HARDENING) && _MSVC_STL_HARDENING == 1 && \
    defined(_MSVC_STL_DESTRUCTOR_TOMBSTONES) && _MSVC_STL_DESTRUCTOR_TOMBSTONES == 1
    constexpr bool stl_hardening = true;
#else
    constexpr bool stl_hardening = false;
#endif
    expect(stl_hardening, "EXTENDED: _MSVC_STL_HARDENING=1 / _MSVC_STL_DESTRUCTOR_TOMBSTONES=1 missing");
#endif
#elif defined(__linux__) && defined(__GNUC__)
    // lorica_deprecate_unsafe_apis: checked by odeum.lorica_applied.unsafe_api.* (build probes).
#if defined(__SSP_STRONG__) || defined(__SSP_ALL__)
    constexpr bool stack_protector = true;
#else
    constexpr bool stack_protector = false;
#endif
    expect(stack_protector, "-fstack-protector-strong missing (__SSP_STRONG__ undefined)");
#if defined(__x86_64__)
#if defined(__CET__) && (__CET__ & 3) == 3
    constexpr bool control_flow = true;
#else
    constexpr bool control_flow = false;
#endif
    expect(control_flow, "-fcf-protection=full missing (__CET__ is not 3)");
#elif defined(__aarch64__)
#if defined(__ARM_FEATURE_BTI_DEFAULT) && defined(__ARM_FEATURE_PAC_DEFAULT)
    constexpr bool control_flow = true;
#else
    constexpr bool control_flow = false;
#endif
    expect(control_flow, "-mbranch-protection=standard missing");
#endif
#if defined(__PIE__) || defined(__pie__)
    constexpr bool position_independent = true;
#else
    constexpr bool position_independent = false;
#endif
    expect(position_independent, "position independent executable code (-fPIE) missing");
    // -fstack-clash-protection and -ftrivial-auto-var-init=zero define no macro: they are checked
    // on every compile command by odeum.lorica_applied.compile_commands.
#if defined(_LIBCPP_VERSION)
#if ODEUM_LORICA_TEST_EXTENDED && defined(_LIBCPP_HARDENING_MODE) && \
    _LIBCPP_HARDENING_MODE == _LIBCPP_HARDENING_MODE_EXTENSIVE
    constexpr bool library_checks = true;
#elif !ODEUM_LORICA_TEST_EXTENDED && defined(_LIBCPP_HARDENING_MODE) && \
    _LIBCPP_HARDENING_MODE == _LIBCPP_HARDENING_MODE_FAST
    constexpr bool library_checks = true;
#else
    constexpr bool library_checks = false;
#endif
    expect(library_checks, "_LIBCPP_HARDENING_MODE is not FAST (baseline) / EXTENSIVE (EXTENDED)");
#elif defined(__GLIBCXX__)
#if defined(_GLIBCXX_ASSERTIONS)
    constexpr bool library_checks = true;
#else
    constexpr bool library_checks = false;
#endif
    expect(library_checks, "_GLIBCXX_ASSERTIONS missing");
#if ODEUM_LORICA_TEST_EXTENDED
    note("EXTENDED with libstdc++: nothing extra is visible in the unit; "
         "-ftrivial-auto-var-init=zero is checked by odeum.lorica_applied.compile_commands");
#endif
#else
    expect(false, "unknown C++ standard library");
#endif
#if ODEUM_LORICA_TEST_OPTIMIZED
    // Lorica asks for level 3 from compiler version 12 (GCC or Clang), else 2.
#if (defined(__clang__) && __clang_major__ >= 12) || (!defined(__clang__) && __GNUC__ >= 12)
    constexpr int expected_fortify = 3;
#else
    constexpr int expected_fortify = 2;
#endif
#if defined(_FORTIFY_SOURCE)
    constexpr int fortify = _FORTIFY_SOURCE;
#else
    constexpr int fortify = 0;
#endif
    expect(fortify == expected_fortify, "_FORTIFY_SOURCE is not the level Lorica selects");
#if defined(__USE_FORTIFY_LEVEL)
    constexpr int effective_fortify = __USE_FORTIFY_LEVEL;
#else
    constexpr int effective_fortify = 0;
#endif
    expect(effective_fortify == expected_fortify,
           "glibc fortification is not in effect at the selected level (__USE_FORTIFY_LEVEL)");
#else
    note("unoptimized configuration: Lorica adds no _FORTIFY_SOURCE, not checked");
#endif
#else
    expect(false, "toolchain without a Lorica check in this test");
#endif
}

// Lorica::boundary: the real checked arithmetic and budgets, run (not only included).
void check_boundary() {
    volatile std::size_t largest = std::numeric_limits<std::size_t>::max();
    expect(!lorica::checked_add(largest, std::size_t{1}).has_value(), "checked_add overflow is not nullopt");
    expect(lorica::checked_add(largest - 1, std::size_t{1}) == std::optional<std::size_t>{largest},
           "checked_add below the limit is not exact");
    volatile std::size_t limit = 64;
    lorica::input_budget bytes({limit, limit, limit, 4});
    expect(!bytes.consume(limit + 1, 0, 0), "input_budget accepted limit + 1 bytes");
    expect(bytes.consume(limit, 0, 0), "input_budget refused exactly the byte limit");
    expect(!bytes.consume(1, 0, 0), "input_budget accepted a byte past the spent limit");
    lorica::input_budget elements({limit, limit, limit, 4});
    expect(!elements.consume(0, limit + 1, 0), "input_budget accepted limit + 1 elements");
    expect(elements.consume(0, limit, 0), "input_budget refused exactly the element limit");
    expect(elements.permits_depth(4) && !elements.permits_depth(5), "input_budget depth limit is off by one");
}

} // namespace

int main() {
#if defined(__APPLE__)
    std::printf("ODEUM_LORICA_SKIP: macOS builds without Lorica target hardening "
                "(ODEUM_LORICA_MACOS_UNHARDENED=ON); not checked\n");
    return 77;
#else
    check_toolchain_settings();
    check_boundary();
    for (const auto& failure : failures) std::printf("FAIL: %s\n", failure.c_str());
    std::printf("odeum.lorica_applied.%s: %d checks, %zu failed\n",
                ODEUM_LORICA_TEST_EXTENDED ? "extended" : "baseline", checks, failures.size());
    return failures.empty() ? 0 : 1;
#endif
}
