// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "biglybtc/bencode.hpp"
#include <optional>
namespace biglybtc::core {
enum class SavedSearchError {Format,Root,List,Limit,Memory};
enum class SavedSearchIssueCode {Record,MissingType,Type,UnknownType,MissingId,NewerVersion,DuplicateId,DuplicateUid};
struct SavedSearchIssue {SavedSearchIssueCode code;std::size_t sourceIndex;std::string field;};
enum class SavedSearchKind {Regex=1,Json=2,Plugin=3,Rss=4};
struct SavedSearchEngine {
  std::size_t sourceIndex=0;
  std::optional<std::int64_t> rawType,id,rawSelected,rawSource,rawAzVersion,lastUpdated;
  std::optional<std::int32_t> type,selected,origin,azVersion;
  std::optional<SavedSearchKind> kind;
  // Stored bytes, not decoded display text or trusted/resolved URLs. No cookies,
  // authentication material or update URLs are copied into this projection.
  std::optional<std::string> uid,name,searchUrl;
  bool duplicateId=false,duplicateUid=false;
};
struct SavedSearchEngines {
  std::shared_ptr<const Document> source;
  bool enginesPresent=false,pluginMapPresent=false;
  std::vector<SavedSearchEngine> engines;
  std::vector<SavedSearchIssue> issues;
};
struct SavedSearchLimits {
  Limits document{};
  std::size_t engines=4096,fieldBytes=65536,textBytes=1024*1024,issues=32768;
};
// Original metasearch.config owning, read-only inventory. Every supplied list
// entry retains its source index, including malformed and unsupported entries.
// Long.intValue() fields use Java's low32 bits. Missing selection/source/version
// use original defaults0/0/5; malformed values remain unavailable. Missing ID
// and UID never allocate identities; duplicates are reported, not collapsed.
// This is NOT EngineImpl import, validation of web/regex/JSON definitions,
// plugin availability, a scheduler, selection authority or a network operation.
// Source includes untouched unknown fields; callers must not log source bytes.
std::expected<SavedSearchEngines,SavedSearchError> readSavedSearchEngines(
    std::string_view,SavedSearchLimits={});
} // namespace biglybtc::core
