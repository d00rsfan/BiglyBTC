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
#include <span>

namespace biglybtc::storage {
inline constexpr const char* VerifiedExistingScopeKey="BiglyBTC.verifiedExistingScope";
// A filesystem snapshot, never piece verification or permission to release an
// owner. decode is structural only; validate binds it to the actual subject.
// Default v1 covers the chosen canonical/custom root. Explicit exactPaths
// produces v2 for every original real index, including authenticated links;
// virtual padding must have an empty entry. Neither form grants write access.
class VerifiedExistingScope final {
public:
  using Proof=std::function<std::expected<void,Error>()>;
  static std::expected<VerifiedExistingScope,Error> capture(const ProfileLease&,
      const core::Document&,const compat::OriginalLayout&,const OriginalTorrentIdentity&,
      const std::filesystem::path& directory,std::string_view leaf,
      std::span<const std::filesystem::path> exactPaths={});
  static std::expected<VerifiedExistingScope,Error> decode(const core::Node&);
  const std::string& encoded() const noexcept;
  bool mapped() const noexcept;
  std::expected<void,Error> validate(const ProfileLease&,const core::Document&,
      const compat::OriginalLayout&,const OriginalTorrentIdentity&,
      const std::filesystem::path& directory,std::string_view leaf,
      std::span<const std::filesystem::path> exactPaths={}) const;
  // Bind the exact O_RDONLY descriptor to its APPROVED stamp, not a fresh
  // replacement. The descriptor must outlive this immutable thread-safe proof.
  std::expected<Proof,Error> bind(std::size_t index,const std::filesystem::path&,int descriptor) const;
private:
  struct Data;
  explicit VerifiedExistingScope(std::shared_ptr<const Data> data):data_(std::move(data)){}
  std::shared_ptr<const Data> data_;
};
}
