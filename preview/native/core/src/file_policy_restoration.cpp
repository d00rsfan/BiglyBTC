#include "biglybtc/file_policy_restoration.hpp"
#include <algorithm>

namespace biglybtc::compat {
std::expected<std::vector<FilePolicy>,PolicyError> originalRestoredFilePolicy(
    std::span<const FilePolicy> projected,std::span<const FilePolicy> initial){
  if(projected.size()!=initial.size()||projected.size()>100000){
    return std::unexpected(PolicyError{"Original restored file-policy census differs or exceeds its bound"});
  }
  const auto source=std::ranges::all_of(projected,[](const auto& value){return value==FilePolicy{};})?initial:projected;
  return std::vector<FilePolicy>(source.begin(),source.end());
}
}
