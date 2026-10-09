#include "dpapi_secret_store.hpp"
#include <windows.h>
#include <dpapi.h>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <vector>

namespace odeum::program::windows {
namespace {
std::vector<BYTE> transform(const std::vector<BYTE>& input, bool protect) {
    DATA_BLOB in{static_cast<DWORD>(input.size()), const_cast<BYTE*>(input.data())}, out{};
    const BOOL ok = protect
        ? CryptProtectData(&in, L"Odeum YouTube stream key", nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out)
        : CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out);
    if (!ok) throw std::runtime_error(protect ? "DPAPI could not protect the stream key" : "DPAPI could not read the stored stream key");
    std::vector<BYTE> result(out.pbData, out.pbData + out.cbData);
    SecureZeroMemory(out.pbData, out.cbData);
    LocalFree(out.pbData);
    return result;
}
}

DpapiSecretStore::DpapiSecretStore(std::filesystem::path file) : file_(std::move(file)) {}

std::optional<std::string> DpapiSecretStore::load() {
    std::ifstream input(file_, std::ios::binary);
    if (!input) return std::nullopt;
    const std::vector<BYTE> sealed((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    if (sealed.empty()) return std::nullopt;
    auto plain = transform(sealed, false);
    std::string key(plain.begin(), plain.end());
    SecureZeroMemory(plain.data(), plain.size());
    return key;
}

void DpapiSecretStore::save(const std::string& key) {
    const auto sealed = transform(std::vector<BYTE>(key.begin(), key.end()), true);
    std::filesystem::create_directories(file_.parent_path());
    auto temporary = file_;
    temporary += ".tmp";
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        output.write(reinterpret_cast<const char*>(sealed.data()), static_cast<std::streamsize>(sealed.size()));
        if (!output) throw std::runtime_error("Cannot write the stream key file");
    }
    std::filesystem::rename(temporary, file_);
}

void DpapiSecretStore::erase() {
    std::error_code ignored;
    std::filesystem::remove(file_, ignored);
}
}
