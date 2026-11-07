// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "saved_share.hpp"
#include "torrent_creator.hpp"
namespace biglybtc::tracker {class OwnedTrackerEndpoint;}
namespace biglybtc::sharing {
enum class SharingMode {ReadOnlyPreview,Normal};
struct ShareCatalogRow {SavedShareSelector selected;bool available=false;std::string refusal;};
struct ShareCatalog {
  std::shared_ptr<const core::SavedShares> source;
  std::optional<storage::RecoverySource> sourceKind;
  std::vector<ShareCatalogRow> rows;std::uint64_t generation=0;
  bool needsRecovery=false;std::string warning;
};
struct ShareCreateKey {std::uint64_t session=0,request=0;bool operator==(const ShareCreateKey&)const=default;};
struct ShareCreateReview {ShareCreateKey key;std::filesystem::path source;core::SavedShareKind kind;std::string announceUrl;};
enum class ShareMutationTruth {Committed,Uncertain};
struct ShareMutationOutcome {ShareMutationTruth truth;std::optional<SavedShareSelector> selected;std::string message;};
class SavedSharingService final {
public:
  static std::expected<std::unique_ptr<SavedSharingService>,storage::Error> open(
      const storage::ProfileLease&,const std::filesystem::path&,std::uint64_t,SharingMode);
  ~SavedSharingService();
  std::expected<ShareCatalog,storage::Error> catalog(const storage::ProfileLease&,std::uint64_t);
  std::expected<ShareCreateReview,storage::Error> prepareCreatePersistent(
      const storage::ProfileLease&,std::uint64_t,const std::filesystem::path&,
      core::SavedShareKind,const tracker::OwnedTrackerEndpoint&,std::stop_token receiver={});
  std::expected<ShareMutationOutcome,storage::Error> confirmCreate(
      storage::ProfileLease&,std::uint64_t,ShareCreateKey,engine::CreateProgressCallback={},std::stop_token dispatchStop={});
  std::expected<SavedShareHandle,storage::Error> retainShare(
      const storage::ProfileLease&,std::uint64_t,const SavedShareSelector&,std::uint64_t catalogGeneration);
  std::expected<ShareMutationOutcome,storage::Error> remove(
      storage::ProfileLease&,std::uint64_t,const SavedShareSelector&,std::uint64_t catalogGeneration,std::stop_token={});
  std::expected<void,storage::Error> recover(storage::ProfileLease&,std::uint64_t);
  void cancel()noexcept;
  std::expected<void,storage::Error> close();
private:
  struct Impl;explicit SavedSharingService(std::shared_ptr<Impl>)noexcept;std::shared_ptr<Impl> impl_;
};
} // namespace biglybtc::sharing
