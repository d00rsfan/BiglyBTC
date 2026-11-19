#pragma once
#include "biglybtc/file_policy.hpp"

namespace biglybtc::compat {
struct OriginalFileSelectionIntent {
  std::vector<std::uint8_t> changed;
  bool skipped=false;
};
// Version1 journal data:exact full-count0/1 bytes,at least one selected file,
// exact Boolean integer and no other fields. This conveys intent only.
std::expected<std::string,PolicyError> encodeOriginalFileSelectionIntent(
    std::span<const std::uint8_t> changed,bool skipped);
std::expected<OriginalFileSelectionIntent,PolicyError> readOriginalFileSelectionIntent(
    const core::Node&,std::size_t files);
}
