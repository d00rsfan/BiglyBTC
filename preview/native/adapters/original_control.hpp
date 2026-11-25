#pragma once
#include "original_file_io.hpp"
#include "state_file_io.hpp"
#include <mutex>

namespace biglybtc::storage {
// Original controls stay in active/HASH/fmfileN.dat[.2]. Only these fixed
// authenticated leaves are read or atomically replaced, under the held lease.
// Identity checks detect external changes; pathname check/rename is not an
// atomic transaction against a noncooperating process swapping names.
class OriginalControl final : public std::enable_shared_from_this<OriginalControl> {
public:
  static std::expected<std::shared_ptr<OriginalControl>, storage::Error> open(
      const storage::ProfileLease&, std::string hash, std::size_t index,
      storage::OriginalStorageKind, bool create);
  std::expected<std::optional<std::string>, storage::Error> load() const;
  // Same authenticated candidates; exact map/geometry validator may select the
  // valid backup without publishing, recovering or replacing either candidate.
  std::expected<std::optional<std::string>, storage::Error> loadValidated(
      storage::OriginalFileGeometry) const;
  // Check the exact held profile and indexed control family before another
  // owner uses this retained handle in a conversion transaction.
  std::expected<void,storage::Error> authenticate(const storage::ProfileLease&,
      std::string_view hash,std::size_t index,storage::OriginalStorageKind) const;
  // Read-only fixed candidates under the same directory/name CAS as load().
  // Does not recover,retire or authorize any payload operation.
  std::expected<bool,storage::Error> hasBackupOnly() const;
  storage::OriginalControlPublisher publisher();
  std::expected<void, storage::Error> retire();
private:
  OriginalControl(detail::StateDescriptor profile, detail::StateDescriptor active,
      detail::StateDescriptor dir, std::filesystem::path profilePath, std::string hash,
      std::string leaf, storage::OriginalStorageKind kind)
      : profile_(std::move(profile)), active_(std::move(active)), directory_(std::move(dir)),
        profilePath_(std::move(profilePath)), hash_(std::move(hash)), leaf_(std::move(leaf)), kind_(kind) {}
  void checkDirectories() const;
  void checkObserved(const std::optional<detail::StateStamp>&,const std::optional<detail::StateStamp>&) const;
  void remember(const std::optional<detail::StateStamp>&,const std::optional<detail::StateStamp>&) const;
  std::expected<void, storage::Error> publish(const std::optional<std::string>&, std::string_view);
  detail::StateDescriptor profile_, active_, directory_;
  std::filesystem::path profilePath_;
  std::string hash_, leaf_;
  storage::OriginalStorageKind kind_;
  mutable std::mutex mutex_;
  mutable bool observed_ = false;
  mutable std::optional<detail::StateStamp> primary_, backup_;
};
} // namespace biglybtc::storage
