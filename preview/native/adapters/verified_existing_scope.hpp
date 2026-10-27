#pragma once
#include "original_torrent_identity.hpp"
#include "biglybtc/torrent_layout.hpp"
#include <filesystem>
#include <expected>
#include <string>
#include <string_view>
#include <utility>
#include <functional>
#include <memory>

namespace biglybtc::storage {
inline constexpr const char* VerifiedExistingScopeKey="BiglyBTC.verifiedExistingScope";
// A filesystem snapshot, never piece verification or permission to release an
// owner. decode is structural only; validate binds it to the actual subject.
class VerifiedExistingScope final {
public:
  using Proof=std::function<std::expected<void,Error>()>;
  static std::expected<VerifiedExistingScope,Error> capture(const ProfileLease&,
      const core::Document&,const compat::OriginalLayout&,const OriginalTorrentIdentity&,
      const std::filesystem::path& directory,std::string_view leaf);
  static std::expected<VerifiedExistingScope,Error> decode(const core::Node&);
  const std::string& encoded() const noexcept;
  std::expected<void,Error> validate(const ProfileLease&,const core::Document&,
      const compat::OriginalLayout&,const OriginalTorrentIdentity&,
      const std::filesystem::path& directory,std::string_view leaf) const;
  // Bind the exact O_RDONLY descriptor to its APPROVED stamp, not a fresh
  // replacement. The descriptor must outlive this immutable thread-safe proof.
  std::expected<Proof,Error> bind(std::size_t index,const std::filesystem::path&,int descriptor) const;
private:
  struct Data;
  explicit VerifiedExistingScope(std::shared_ptr<const Data> data):data_(std::move(data)){}
  std::shared_ptr<const Data> data_;
};
}
