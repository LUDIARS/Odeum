#include "amf0.hpp"
#include "bytes.hpp"
#include <bit>

namespace odeum::program {
namespace {
enum Marker : std::uint32_t { number = 0, boolean = 1, string = 2, object = 3, null = 5, undefined = 6, ecma_array = 8, object_end = 9, long_string = 12 };
constexpr int max_depth = 16;

void encode_key(std::vector<std::byte>& out, std::string_view key) {
    if (key.size() > 0xffff) throw Amf0Error("AMF0 property name too long");
    put_u16(out, static_cast<std::uint32_t>(key.size()));
    put_text(out, key);
}

void encode_properties(std::vector<std::byte>& out, const std::vector<Amf0Property>& properties) {
    for (const auto& property : properties) {
        encode_key(out, property.key);
        amf0_encode(out, property.value);
    }
    put_u16(out, 0);
    put_u8(out, object_end);
}

class Reader {
public:
    explicit Reader(std::span<const std::byte> data) : data_(data) {}
    bool done() const noexcept { return at_ >= data_.size(); }
    Amf0Value value(int depth) {
        if (depth > max_depth) throw Amf0Error("AMF0 nesting too deep");
        const auto marker = u8();
        switch (marker) {
        case number: return Amf0Value::of(f64());
        case boolean: return Amf0Value::of(u8() != 0);
        case string: return Amf0Value::of(text(u16()));
        case long_string: return Amf0Value::of(text(u32()));
        case null: return Amf0Value::null();
        case undefined: { Amf0Value v; v.type = Amf0Value::Type::undefined; return v; }
        case object: return Amf0Value::object(properties(depth));
        case ecma_array: u32(); return Amf0Value::ecma_array(properties(depth)); // the count is only a hint
        default: throw Amf0Error("Unsupported AMF0 marker");
        }
    }
private:
    void need(std::size_t n) const { if (data_.size() - at_ < n) throw Amf0Error("Truncated AMF0 data"); }
    std::uint32_t u8() { need(1); return get_u8(data_, at_++); }
    std::uint32_t u16() { need(2); const auto v = get_u16(data_, at_); at_ += 2; return v; }
    std::uint32_t u32() { need(4); const auto v = get_u32(data_, at_); at_ += 4; return v; }
    double f64() {
        const std::uint64_t high = u32(), low = u32();
        return std::bit_cast<double>(high << 32 | low);
    }
    std::string text(std::size_t n) {
        need(n);
        std::string result(reinterpret_cast<const char*>(data_.data() + at_), n);
        at_ += n;
        return result;
    }
    std::vector<Amf0Property> properties(int depth) {
        std::vector<Amf0Property> result;
        for (;;) {
            auto key = text(u16());
            if (key.empty()) {
                need(1);
                if (get_u8(data_, at_) == object_end) { ++at_; return result; }
            }
            result.push_back({std::move(key), value(depth + 1)});
        }
    }
    std::span<const std::byte> data_;
    std::size_t at_ = 0;
};
}

Amf0Value Amf0Value::of(double value) { Amf0Value v; v.type = Type::number; v.number = value; return v; }
Amf0Value Amf0Value::of(bool value) { Amf0Value v; v.type = Type::boolean; v.boolean = value; return v; }
Amf0Value Amf0Value::of(std::string value) { Amf0Value v; v.type = Type::string; v.string = std::move(value); return v; }
Amf0Value Amf0Value::of(const char* value) { return of(std::string(value)); }
Amf0Value Amf0Value::object(std::vector<Amf0Property> properties) { Amf0Value v; v.type = Type::object; v.properties = std::move(properties); return v; }
Amf0Value Amf0Value::ecma_array(std::vector<Amf0Property> properties) { Amf0Value v; v.type = Type::ecma_array; v.properties = std::move(properties); return v; }
Amf0Value Amf0Value::null() { return {}; }

const Amf0Value* Amf0Value::find(std::string_view key) const {
    for (const auto& property : properties) if (property.key == key) return &property.value;
    return nullptr;
}

bool Amf0Value::operator==(const Amf0Value&) const = default;

void amf0_encode(std::vector<std::byte>& out, const Amf0Value& value) {
    switch (value.type) {
    case Amf0Value::Type::number: put_u8(out, number); put_f64(out, value.number); break;
    case Amf0Value::Type::boolean: put_u8(out, boolean); put_u8(out, value.boolean ? 1 : 0); break;
    case Amf0Value::Type::string:
        if (value.string.size() > 0xffff) { put_u8(out, long_string); put_u32(out, static_cast<std::uint32_t>(value.string.size())); }
        else { put_u8(out, string); put_u16(out, static_cast<std::uint32_t>(value.string.size())); }
        put_text(out, value.string);
        break;
    case Amf0Value::Type::object: put_u8(out, object); encode_properties(out, value.properties); break;
    case Amf0Value::Type::ecma_array:
        put_u8(out, ecma_array);
        put_u32(out, static_cast<std::uint32_t>(value.properties.size()));
        encode_properties(out, value.properties);
        break;
    case Amf0Value::Type::null: put_u8(out, null); break;
    case Amf0Value::Type::undefined: put_u8(out, undefined); break;
    }
}

std::vector<std::byte> amf0_encode(const std::vector<Amf0Value>& values) {
    std::vector<std::byte> out;
    for (const auto& value : values) amf0_encode(out, value);
    return out;
}

std::vector<Amf0Value> amf0_decode(std::span<const std::byte> data) {
    Reader reader(data);
    std::vector<Amf0Value> values;
    while (!reader.done()) values.push_back(reader.value(0));
    return values;
}
}
