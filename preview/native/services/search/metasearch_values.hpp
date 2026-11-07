// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "biglybtc/bencode.hpp"
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include <utility>
namespace biglybtc::search {
enum class MetasearchValueError {Malformed,Unsupported,Ambiguous,Inactive,Limit,Memory};
enum class MetasearchRecipeKind {Rss,Json,Regex};
struct SavedMetasearchSelector {
  std::size_t sourceIndex=0;
  std::int64_t id=0;
  std::string uid; // Exact saved bytes, not display text.
  bool operator==(const SavedMetasearchSelector&)const=default;
};
struct MetasearchMapping {
  std::string name;std::int32_t field=0;std::string postFilter;
  MetasearchMapping(std::string source,std::int32_t target,std::string filter={})
      :name(std::move(source)),field(target),postFilter(std::move(filter)){}
};
struct MetasearchPatternInfo {std::size_t captures=0;bool nullable=false;};
// Syntax admission only, never an effect permit or an unbounded matcher.
std::expected<MetasearchPatternInfo,MetasearchValueError> inspectMetasearchPattern(std::string_view);
struct MetasearchRecipe {
  MetasearchRecipeKind kind=MetasearchRecipeKind::Rss;
  std::string configuredUrl,resultPath,resultPattern;
  std::vector<MetasearchMapping> mappings; // Original list order is observable.
};
std::expected<void,MetasearchValueError> validateMetasearchRecipeQuery(const MetasearchRecipe&,std::string_view);
struct MetasearchCatalogRow {
  SavedMetasearchSelector selected;
  std::string name;
  std::optional<std::int32_t> type,selectionState;
  std::optional<MetasearchValueError> issue;
  std::string diagnostic;
  std::optional<MetasearchRecipe> recipe;
};
struct MetasearchCatalog {
  std::shared_ptr<const core::Document> source; // Raw/unknown bytes retained.
  std::vector<MetasearchCatalogRow> rows;
};
// Passive bounded decoding only. Actual source owners must reopen real files.
std::expected<MetasearchCatalog,MetasearchValueError> readMetasearchCatalog(std::string);
// Java URLEncoder UTF8: space '+', '*' safe, '~' encoded. Only %s is a
// parameter; existing well-formed %HH escapes remain unchanged.
std::expected<std::string,MetasearchValueError> expandMetasearchQuery(
    std::string_view actualConfiguredTemplate,std::string_view queryUtf8);
struct MetasearchResultKey {
  std::uint64_t queryNonce=0,rowId=0;
  bool operator==(const MetasearchResultKey&)const=default;
};
struct MetasearchRowValues {
  std::optional<std::string> title,publicationText,detailsLink,torrentLink,downloadButtonLink,hash,uid,rawTorrentLink;
  std::int64_t size=-1;
  std::int32_t seeds=-1,peers=-1;
  bool incomplete=false; // Unsupported/uncertain projection never authorizes intake.
};
} // namespace biglybtc::search
