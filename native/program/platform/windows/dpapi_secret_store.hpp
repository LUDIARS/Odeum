#pragma once
#include "../../core/media_codecs.hpp"
#include <filesystem>

namespace odeum::program::windows {
// The YouTube stream key encrypted with DPAPI for the current user (CryptProtectData), in
// %APPDATA%\Odeum\program-stream-key.bin. The plain key exists only in memory.
class DpapiSecretStore final : public SecretStore {
public:
    explicit DpapiSecretStore(std::filesystem::path file);
    std::optional<std::string> load() override;
    void save(const std::string& key) override;
    void erase() override;
private:
    std::filesystem::path file_;
};
}
