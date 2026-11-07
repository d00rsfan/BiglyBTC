// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "saved_metasearch_source.hpp"
#include <array>
#include <stop_token>
#include <string_view>
namespace biglybtc::search {
class MetasearchMetainfoFetch;
class MetasearchMagnetAdmission;
struct SavedMetasearchResultDescription : RssGetDescription {
  MetasearchResultKey key;
  SavedMetasearchSelector engine;
  std::string query;
  std::optional<std::array<std::byte,20>> expectedV1Hash;
  std::optional<std::string> referer;
};
// Only the actual service can mint this from its retained real parsed result.
// A UI key, link, hash or caller-created row can never construct one.
class SavedMetasearchResultHandle final {
public:
  SavedMetasearchResultHandle(SavedMetasearchResultHandle&&)noexcept;
  SavedMetasearchResultHandle& operator=(SavedMetasearchResultHandle&&)noexcept;
  ~SavedMetasearchResultHandle();
  SavedMetasearchResultHandle(const SavedMetasearchResultHandle&)=delete;
  const SavedMetasearchResultDescription& description()const noexcept;
  std::expected<std::uint64_t,SavedRssSourceError> current(
      const storage::ProfileLease&,std::uint64_t)const;
private:
  friend class SavedMetasearchService;
  friend class RssHttpFetch;
  friend class MetasearchMetainfoFetch;
class MetasearchMagnetAdmission;
  static std::expected<SavedMetasearchResultHandle,SavedRssSourceError> mint(
      const storage::ProfileLease&,std::uint64_t,
      std::shared_ptr<const SavedMetasearchSourceSnapshot>,const MetasearchRowValues&,
      MetasearchResultKey,std::shared_ptr<std::stop_source>);
  std::expected<SavedMetasearchResultHandle,SavedRssSourceError> retainedCopy(
      const storage::ProfileLease&,std::uint64_t)const;
  bool sameIdentity(const SavedMetasearchResultHandle&)const noexcept;
  struct Impl;explicit SavedMetasearchResultHandle(std::shared_ptr<const Impl>)noexcept;
  std::shared_ptr<const Impl> impl_;
};
// Distinct from the HTTP result handle. Only a genuine retained parsed magnet
// row and its actual query/download receiver can mint this capability.
struct SavedMetasearchMagnetDescription {
  MetasearchResultKey key;
  SavedMetasearchSelector engine;
  std::string query,name;
};
class SavedMetasearchMagnetHandle final {
public:
  SavedMetasearchMagnetHandle(SavedMetasearchMagnetHandle&&)noexcept;
  SavedMetasearchMagnetHandle& operator=(SavedMetasearchMagnetHandle&&)noexcept;
  ~SavedMetasearchMagnetHandle();
  SavedMetasearchMagnetHandle(const SavedMetasearchMagnetHandle&)=delete;
  const SavedMetasearchMagnetDescription& description()const noexcept;
  std::expected<std::uint64_t,SavedRssSourceError> current(
      const storage::ProfileLease&,std::uint64_t)const;
private:
  friend class SavedMetasearchService;
  friend class MetasearchMagnetAdmission;
  static std::expected<SavedMetasearchMagnetHandle,SavedRssSourceError> mint(
      const storage::ProfileLease&,std::uint64_t,
      std::shared_ptr<const SavedMetasearchSourceSnapshot>,const MetasearchRowValues&,
      MetasearchResultKey,std::shared_ptr<std::stop_source>,std::stop_token);
  std::string_view uri()const noexcept;
  std::stop_token queryToken()const noexcept;
  std::stop_token receiverToken()const noexcept;
  const std::optional<std::array<std::byte,20>>& expectedV1Hash()const noexcept;
  std::expected<SavedMetasearchMagnetHandle,SavedRssSourceError> retainedCopy(
      const storage::ProfileLease&,std::uint64_t)const;
  struct Impl;explicit SavedMetasearchMagnetHandle(std::shared_ptr<const Impl>)noexcept;
  std::shared_ptr<const Impl> impl_;
};
} // namespace biglybtc::search
