#pragma once

#include <string>
#include <string_view>

namespace biglybtc::probe {

// Compatibility experiment for CryptoECCUtils, never a new identity policy.
bool verifyLegacySignature(std::string_view publicKey, std::string_view message,
    std::string_view signature);
std::string signLegacyMessage(std::string_view privateKey, std::string_view publicKey,
    std::string_view message);

} // namespace biglybtc::probe
