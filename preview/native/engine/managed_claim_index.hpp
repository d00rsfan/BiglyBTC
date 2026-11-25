// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "managed_payload_claim.hpp"
#include "state_file_io.hpp"
#include <set>
#include <span>
namespace biglybtc::engine::detail {
struct OwnerClaims {
  std::uint64_t admission;
  std::span<const ManagedPayloadClaim> files;
};
// Claims are immutable for the lifetime of an admission. A removed or replaced
// owner invalidates the prefix previously checked on the session worker.
class ManagedClaimIndex {
public:
  void refresh(std::span<const OwnerClaims> owners);
  void append(std::span<const ManagedPayloadClaim> files);
  void check(std::span<const ManagedPayloadClaim> candidate)const;
private:
  std::set<std::uint64_t> owners_;
  std::set<std::filesystem::path,storage::detail::FilesystemPathLess> paths_;
  std::set<std::pair<std::uint64_t,std::uint64_t>> inodes_;
  std::size_t count_=0;
};
}
