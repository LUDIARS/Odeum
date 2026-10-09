#import <Foundation/Foundation.h>
#import <Security/Security.h>
#include "keychain_secret_store.hpp"
#include <stdexcept>
#include <string>

namespace odeum::program::macos {
namespace {
NSMutableDictionary* query() {
    return [@{(__bridge id)kSecClass: (__bridge id)kSecClassGenericPassword,
              (__bridge id)kSecAttrService: @"com.ludiars.odeum.program",
              (__bridge id)kSecAttrAccount: @"youtube-stream-key"} mutableCopy];
}
}

std::optional<std::string> KeychainSecretStore::load() {
    @autoreleasepool {
        auto* search = query();
        search[(__bridge id)kSecReturnData] = @YES;
        search[(__bridge id)kSecMatchLimit] = (__bridge id)kSecMatchLimitOne;
        CFTypeRef result = nullptr;
        const auto status = SecItemCopyMatching((__bridge CFDictionaryRef)search, &result);
        if (status == errSecItemNotFound) return std::nullopt;
        if (status != errSecSuccess || !result) throw std::runtime_error("The Keychain could not read the stream key (" + std::to_string(status) + ")");
        NSData* data = (__bridge_transfer NSData*)result;
        return std::string(static_cast<const char*>(data.bytes), data.length);
    }
}

void KeychainSecretStore::save(const std::string& key) {
    @autoreleasepool {
        NSData* data = [NSData dataWithBytes:key.data() length:key.size()];
        auto* existing = query();
        const auto updated = SecItemUpdate((__bridge CFDictionaryRef)existing, (__bridge CFDictionaryRef)@{(__bridge id)kSecValueData: data});
        if (updated == errSecSuccess) return;
        if (updated != errSecItemNotFound) throw std::runtime_error("The Keychain could not update the stream key (" + std::to_string(updated) + ")");
        auto* item = query();
        item[(__bridge id)kSecValueData] = data;
        item[(__bridge id)kSecAttrAccessible] = (__bridge id)kSecAttrAccessibleAfterFirstUnlockThisDeviceOnly;
        const auto added = SecItemAdd((__bridge CFDictionaryRef)item, nullptr);
        if (added != errSecSuccess) throw std::runtime_error("The Keychain could not store the stream key (" + std::to_string(added) + ")");
    }
}

void KeychainSecretStore::erase() {
    @autoreleasepool {
        const auto status = SecItemDelete((__bridge CFDictionaryRef)query());
        if (status != errSecSuccess && status != errSecItemNotFound) throw std::runtime_error("The Keychain could not delete the stream key");
    }
}
}
