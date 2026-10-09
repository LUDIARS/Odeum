#include <odeum/ticket.hpp>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <array>
#include <vector>

namespace odeum {
struct TicketVerifier::Key {
    EVP_PKEY* value;
    explicit Key(EVP_PKEY* key) : value(key) {}
    ~Key() { EVP_PKEY_free(value); }
};
namespace {
[[noreturn]] void invalid() { throw ProtocolError("invalid_ticket", "Invalid ticket"); }
std::string decode(std::string_view encoded) {
    if (encoded.empty() || encoded.size() % 4 == 1) invalid();
    std::string out; unsigned bits = 0, value = 0;
    for (char c : encoded) {
        int n = c >= 'A' && c <= 'Z' ? c - 'A' : c >= 'a' && c <= 'z' ? c - 'a' + 26 :
            c >= '0' && c <= '9' ? c - '0' + 52 : c == '-' ? 62 : c == '_' ? 63 : -1;
        if (n < 0) invalid(); value = (value << 6) | static_cast<unsigned>(n); bits += 6;
        if (bits >= 8) { bits -= 8; out.push_back(static_cast<char>((value >> bits) & 255)); }
    }
    if (bits && (value & ((1u << bits) - 1u))) invalid();
    return out;
}
// A SHA-256 digest in unpadded base64url is exactly 43 characters and decodes to 32 bytes.
std::string digest(const Json& value) {
    if (!value.is_string() || value.get_ref<const std::string&>().size() != 43 || decode(value.get<std::string>()).size() != 32) invalid();
    return value.get<std::string>();
}
}
TicketVerifier::TicketVerifier(const Json& public_keys) {
    if (!public_keys.is_object() || public_keys.empty()) throw std::runtime_error("Public keys must be a nonempty object");
    for (const auto& [kid, pem] : public_keys.items()) {
        if (kid.empty() || !pem.is_string()) throw std::runtime_error("Invalid public key entry");
        const auto text = pem.get<std::string>();
        std::unique_ptr<BIO, decltype(&BIO_free)> bio(BIO_new_mem_buf(text.data(), static_cast<int>(text.size())), BIO_free);
        auto key = std::make_shared<Key>(bio ? PEM_read_bio_PUBKEY(bio.get(), nullptr, nullptr, nullptr) : nullptr);
        if (!key->value || EVP_PKEY_id(key->value) != EVP_PKEY_ED25519) throw std::runtime_error("Public key must be Ed25519 PEM");
        keys_.emplace(kid, std::move(key));
    }
}
std::string sender_slot(const Ticket& ticket) {
    if (ticket.role == Role::producer) return std::string(program_slot);
    if (ticket.role != Role::presenter) return {};
    return ticket.slot.empty() ? std::string(program_slot) : ticket.slot;
}
Ticket TicketVerifier::verify(std::string_view compact, std::int64_t now) const {
    try {
        if (compact.size() > 8192) invalid();
        auto a = compact.find('.'), b = compact.find('.', a == std::string_view::npos ? 0 : a + 1);
        if (a == std::string_view::npos || b == std::string_view::npos || compact.find('.', b + 1) != std::string_view::npos) invalid();
        auto h = Json::parse(decode(compact.substr(0, a)));
        if (h.at("alg") != "EdDSA" || h.contains("crit") || h.contains("b64")) invalid();
        auto key = keys_.find(h.at("kid").get<std::string>()); if (key == keys_.end()) invalid();
        auto signature = decode(compact.substr(b + 1)); if (signature.size() != 64) invalid();
        std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> ctx(EVP_MD_CTX_new(), EVP_MD_CTX_free);
        if (!ctx || EVP_DigestVerifyInit(ctx.get(), nullptr, nullptr, nullptr, key->second->value) != 1 ||
            EVP_DigestVerify(ctx.get(), reinterpret_cast<const unsigned char*>(signature.data()), signature.size(),
                reinterpret_cast<const unsigned char*>(compact.data()), b) != 1) invalid();
        const auto j = Json::parse(decode(compact.substr(a + 1, b - a - 1)));
        if (j.at("iss") != "glab" || j.at("aud") != "odeum-relay") invalid();
        require_text(j, "sub", 256); require_text(j, "name", 64, true); require_text(j, "jti", 256);
        if (now < 0 || now > INT64_MAX - 300) invalid();
        if (!j.at("exp").is_number_integer() || (j.at("exp").is_number_unsigned() && j.at("exp").get<std::uint64_t>() > INT64_MAX)) invalid();
        auto exp = j.at("exp").get<std::int64_t>(); if (exp <= now || exp > now + 300) invalid();
        if (j.contains("iat")) {
            if (!j["iat"].is_number_integer() || j["iat"] < 0 || j["iat"] > now || j["iat"] < exp - 300) invalid();
        }
        auto role = parse_role(j.at("role").get<std::string>());
        if (role != Role::service) require_text(j, "sid", 256);
        Ticket ticket{j["sub"], j["name"], role == Role::service ? "" : j["sid"].get<std::string>(), j["jti"], role, exp};
        const bool sender = role == Role::presenter || role == Role::producer;
        if (j.contains("slot")) {
            const auto& slot = j.at("slot");
            if (!sender || !slot.is_string() || !valid_slot(slot.get_ref<const std::string&>())) invalid();
            if (role == Role::producer && slot.get_ref<const std::string&>() != program_slot) invalid();
            ticket.slot = slot.get<std::string>();
        } else if (sender) ticket.slot = program_slot;
        if (j.contains("invite")) {
            const auto& invite = j.at("invite");
            if (!sender || !invite.is_object() || invite.size() != 2) invalid();
            ticket.invite_join = digest(invite.at("join")); ticket.invite_overlay = digest(invite.at("overlay"));
            if (ticket.invite_join == ticket.invite_overlay) invalid();
        }
        return ticket;
    } catch (const Json::exception&) { invalid(); }
    catch (const ProtocolError&) { invalid(); }
}
}
