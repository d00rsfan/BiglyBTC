// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "saved_rss_source.hpp"
#include "metasearch_values.hpp"
namespace biglybtc::search {
class RssHttpFetch;
class SavedMetasearchService;
class SavedMetasearchResultHandle;
struct SavedMetasearchDescription : RssGetDescription {
  SavedMetasearchSelector selected;
  std::string query;
  std::shared_ptr<const MetasearchRecipe> recipe;
};
// Read-only real configured source/query observation; never caller URL authority.
class SavedMetasearchSourceSnapshot final {
public:
  static std::expected<SavedMetasearchSourceSnapshot,SavedRssSourceError> read(
      const storage::ProfileLease&,const std::filesystem::path&,std::uint64_t,
      SavedMetasearchSelector,std::string queryUtf8);
  SavedMetasearchSourceSnapshot(SavedMetasearchSourceSnapshot&&)noexcept;
  SavedMetasearchSourceSnapshot& operator=(SavedMetasearchSourceSnapshot&&)noexcept;
  ~SavedMetasearchSourceSnapshot();
  SavedMetasearchSourceSnapshot(const SavedMetasearchSourceSnapshot&)=delete;
  const SavedMetasearchDescription& description()const noexcept;
  std::expected<std::uint64_t,SavedRssSourceError> current(
      const storage::ProfileLease&,std::uint64_t)const;
private:
  friend class SavedMetasearchResultHandle;
  std::expected<RssGetDescription,SavedRssSourceError> resultDescription(
      const storage::ProfileLease&,std::uint64_t,std::string_view actualParsedDownloadLink)const;
  struct Impl;explicit SavedMetasearchSourceSnapshot(std::unique_ptr<Impl>)noexcept;
  std::unique_ptr<Impl> impl_;
};
} // namespace biglybtc::search
