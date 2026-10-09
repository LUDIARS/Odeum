#pragma once
#include "../../core/media_codecs.hpp"

namespace odeum::program::macos {
// The YouTube stream key as a generic password in the user's login Keychain
// (service com.ludiars.odeum.program, account youtube-stream-key).
class KeychainSecretStore final : public SecretStore {
public:
    std::optional<std::string> load() override;
    void save(const std::string& key) override;
    void erase() override;
};
}
