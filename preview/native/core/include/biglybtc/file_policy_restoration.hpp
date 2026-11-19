#pragma once
#include "biglybtc/file_policy.hpp"

namespace biglybtc::compat {
// Both spans must describe the same complete original file census. The saved
// span must come from successful strict originalFilePolicy projection. Only a
// saved Long narrowing to -1 projects to ordinary {false,0};missing slots have
// the same projection. If all are ordinary,Original keeps constructor values.
// This returns values only;no cache cleanup,write or file-existence authority.
std::expected<std::vector<FilePolicy>,PolicyError> originalRestoredFilePolicy(
    std::span<const FilePolicy> projected,std::span<const FilePolicy> initial);
}
