#pragma once
#include <windows.h>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace odeum::program::windows {
// Throws std::runtime_error naming the call and the HRESULT.
inline void check(HRESULT result, const char* what) {
    if (SUCCEEDED(result)) return;
    std::ostringstream message;
    message << what << " failed (0x" << std::hex << std::setw(8) << std::setfill('0') << static_cast<unsigned long>(result) << ")";
    throw std::runtime_error(message.str());
}
}
