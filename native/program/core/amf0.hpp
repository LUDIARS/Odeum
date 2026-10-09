#pragma once
#include <cstddef>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace odeum::program {
struct Amf0Property;

// The AMF0 values RTMP commands and onMetaData use. Objects keep their property order.
struct Amf0Value {
    enum class Type { number, boolean, string, object, null, undefined, ecma_array };
    Type type = Type::null;
    double number{};
    bool boolean{};
    std::string string;
    std::vector<Amf0Property> properties; // object and ecma_array

    static Amf0Value of(double value);
    static Amf0Value of(bool value);
    static Amf0Value of(std::string value);
    static Amf0Value of(const char* value);
    static Amf0Value object(std::vector<Amf0Property> properties);
    static Amf0Value ecma_array(std::vector<Amf0Property> properties);
    static Amf0Value null();
    // The property's value, or nullptr when absent (object and ecma_array only).
    const Amf0Value* find(std::string_view key) const;
    bool operator==(const Amf0Value&) const;
};

struct Amf0Property {
    std::string key;
    Amf0Value value;
    bool operator==(const Amf0Property&) const = default;
};

struct Amf0Error : std::invalid_argument { using invalid_argument::invalid_argument; };

void amf0_encode(std::vector<std::byte>& out, const Amf0Value& value);
std::vector<std::byte> amf0_encode(const std::vector<Amf0Value>& values);
// Every value in the buffer, in order. Throws Amf0Error for a truncated buffer, an unknown
// marker or nesting deeper than 16 levels.
std::vector<Amf0Value> amf0_decode(std::span<const std::byte> data);
}
