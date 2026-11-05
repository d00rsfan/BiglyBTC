// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "biglybtc/saved_categories.hpp"
#include "biglybtc/library.hpp"
#include "profile.hpp"
#include <chrono>
#include <stop_token>

namespace biglybtc::storage {
class CategoryStore;
class CategorySnapshot final {
public:
  CategorySnapshot()=default;
  bool valid()const noexcept{return bool(data_);}
  std::uint64_t generation()const noexcept;
  bool missing()const noexcept;
  bool writable()const noexcept; // Captured hint; never permission to write.
  const core::SavedCategories* categories()const noexcept;
private:
  struct Data;friend class CategoryStore;
  explicit CategorySnapshot(std::shared_ptr<const Data> data):data_(std::move(data)){}
  std::shared_ptr<const Data> data_;
};
class CategoryTargetSnapshot final {
public:
  CategoryTargetSnapshot()=default;
  bool valid()const noexcept{return bool(data_);}
  std::uint64_t generation()const noexcept;
  bool writable()const noexcept;
  const core::TorrentHash* hash()const noexcept;
  const core::SavedCategoryAssignment* assignment()const noexcept;
private:
  struct Data;friend class CategoryStore;
  explicit CategoryTargetSnapshot(std::shared_ptr<const Data> data):data_(std::move(data)){}
  std::shared_ptr<const Data> data_;
};
struct CategoryCommit {CategorySnapshot snapshot;bool changed=false;std::optional<std::string> createdName;};
struct CategoryAssignmentCommit {CategoryTargetSnapshot snapshot;bool changed=false;};
// Synchronous construction-worker only. Caller owns scheduling/serialization.
// Snapshots own data only; the store retains ONE actual same-OFD profile lease.
// No QObject, thread/pool, timer, manager, payload or network owner is created.
class CategoryStore final {
public:
  using Deadline=std::chrono::steady_clock::time_point;
  static std::expected<std::unique_ptr<CategoryStore>,Error> open(
      const ProfileLease&,std::stop_token={},Deadline=Deadline::max());
  ~CategoryStore();CategoryStore(const CategoryStore&)=delete;CategoryStore& operator=(const CategoryStore&)=delete;
  // Closed/poisoned instance needs explicit close/open. A healthy snapshot may
  // separately be read-only due to preserved candidates; reopening repairs none.
  bool recoveryRequired()const noexcept;
  std::expected<CategorySnapshot,Error> snapshot()const;
  std::expected<CategorySnapshot,Error> reload(std::stop_token={},Deadline=Deadline::max());
  std::expected<CategoryCommit,Error> create(const CategorySnapshot&,std::string name,
      std::stop_token={},Deadline=Deadline::max());
  // One current-target slot. Fresh inventory/full-hash/state/cache preimages
  // are held privately; a UI scalar hash or old/foreign snapshot cannot apply.
  std::expected<CategoryTargetSnapshot,Error> readTarget(const CategorySnapshot&,
      const core::TorrentHash&,std::stop_token={},Deadline=Deadline::max());
  std::expected<CategoryAssignmentCommit,Error> assign(const CategorySnapshot&,
      const CategoryTargetSnapshot&,const std::optional<std::string>& category,
      std::stop_token={},Deadline=Deadline::max());
  // Explicitly unavailable until an inventory-complete multi-file category
  // journal/publication contract exists; never reports partial deletion Saved.
  std::expected<CategoryCommit,Error> remove(const CategorySnapshot&,std::string_view);
  std::expected<void,Error> close();
private:
  struct Impl;explicit CategoryStore(std::unique_ptr<Impl>);
  std::unique_ptr<Impl> impl_;
};
} // namespace biglybtc::storage
