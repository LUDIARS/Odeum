// libFuzzer entry: a JWS compact ticket (protocol/ticket.cpp). Signatures will not verify, so this
// exercises the framing, base64url and header JSON before the signature check.
#include <odeum/ticket.hpp>
#include <cstddef>
#include <cstdint>

namespace {
const odeum::TicketVerifier& verifier() {
    // A fixed public key (RFC 8032 test vector 1); any Ed25519 key works for parsing.
    static const odeum::TicketVerifier instance(odeum::Json{{"key",
        "-----BEGIN PUBLIC KEY-----\nMCowBQYDK2VwAyEA11qYAYKxCrfVS/7TyWQHOg7hcvPapiMlrwIaaPcHURo=\n-----END PUBLIC KEY-----\n"}});
    return instance;
}
}

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    try {
        (void)verifier().verify({reinterpret_cast<const char*>(data), size}, 1000);
    } catch (const odeum::ProtocolError&) {
    }
    return 0;
}
