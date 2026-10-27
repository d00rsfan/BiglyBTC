#pragma once
#include "verified_existing_scope.hpp"
#include "original_file_io.hpp"
#include "biglybtc/file_policy.hpp"

namespace biglybtc::storage {
inline constexpr const char* VerifiedSelectedScopeKey="BiglyBTC.verifiedSelectedScope";
// Separate from Full's scope. A digest binds the exact observed source/map,
// policy, physical kinds, real-leaf/ancestor stamps and original controls.
// Decode is structural only; every authority consumer must call validate.
class SelectedExistingScope final {
public:
  static std::expected<SelectedExistingScope,Error> capture(const ProfileLease&,
      const core::Document&,const compat::OriginalLayout&,const OriginalTorrentIdentity&,
      std::span<const compat::FilePolicy>,std::span<const OriginalStorageKind>,
      std::span<const std::filesystem::path>);
  static std::expected<SelectedExistingScope,Error> decode(const core::Node&);
  const std::string& digest()const noexcept{return digest_;}
  std::string encoded()const{return "d6:digest64:"+digest_+"7:versioni1ee";}
  std::expected<void,Error> validate(const ProfileLease&,const core::Document&,
      const compat::OriginalLayout&,const OriginalTorrentIdentity&,
      std::span<const compat::FilePolicy>,std::span<const OriginalStorageKind>,
      std::span<const std::filesystem::path>)const;
private:
  explicit SelectedExistingScope(std::string value):digest_(std::move(value)){}
  std::string digest_;
};
}
