// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "saved_metasearch_result.hpp"
#include "metasearch_http_fetch.hpp"
#include <functional>
namespace biglybtc::search {
enum class MetasearchServiceMode {ReadOnlyPreview,Normal};
enum class MetasearchServiceError {Source,WrongWorker,Unsupported,Busy,Cancelled,Limit,Closed,Memory,Fetch,Parse};
struct MetasearchResultRecord {MetasearchResultKey key;SavedMetasearchSelector engine;MetasearchRowValues values;};
struct MetasearchEngineOutcome {
  SavedMetasearchSelector engine;
  std::vector<MetasearchResultRecord> rows;
  std::optional<MetasearchServiceError> failure;
  std::string diagnostic;
  bool partial=false;
};
struct MetasearchSearchUpdate {
  std::uint64_t queryNonce=0;
  std::size_t completed=0,total=0;
  bool done=false,cancelled=false,hadFailures=false;
  MetasearchEngineOutcome engine; // One actual completed engine per update.
};
using MetasearchFetchFactory=std::function<std::unique_ptr<MetasearchHttpFetch>(
    const storage::ProfileLease&,const std::filesystem::path&,std::uint64_t,
    const SavedMetasearchSelector&,const std::string&,std::stop_token)>;
// Serialized profile-worker only. One bounded serial federation, no timer,
// thread pool, persistent engine edits, due check, selection/startup search.
class SavedMetasearchService final {
public:
  static std::expected<std::unique_ptr<SavedMetasearchService>,MetasearchServiceError> open(
      const storage::ProfileLease&,std::filesystem::path,std::uint64_t,
      MetasearchServiceMode,MetasearchFetchFactory={});
  ~SavedMetasearchService();
  std::expected<std::vector<MetasearchCatalogRow>,MetasearchServiceError> catalog(
      const storage::ProfileLease&,std::uint64_t);
  std::expected<std::uint64_t,MetasearchServiceError> beginSearch(
      const storage::ProfileLease&,std::uint64_t,std::vector<SavedMetasearchSelector>,
      std::string queryUtf8,std::stop_token receiver={});
  std::expected<std::optional<MetasearchSearchUpdate>,MetasearchServiceError> poll(
      const storage::ProfileLease&,std::uint64_t);
  std::expected<SavedMetasearchResultHandle,MetasearchServiceError> retainResult(
      const storage::ProfileLease&,std::uint64_t,MetasearchResultKey);
  bool active()const noexcept;
  void cancel()noexcept; // Worker only; GUI uses its shared receiver token.
  std::expected<void,MetasearchServiceError> close();
private:
  struct Impl;explicit SavedMetasearchService(std::unique_ptr<Impl>);
  std::unique_ptr<Impl> impl_;
};
} // namespace biglybtc::search
