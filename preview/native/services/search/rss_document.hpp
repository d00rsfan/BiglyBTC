// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QByteArray>
#include <QString>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <vector>
namespace biglybtc::search {
enum class RssError {Xml,ForbiddenDtd,Limit,Memory};
enum class RssIssueCode {UnavailableHead,UnsupportedUrl,InvalidNumber,UnsupportedNumber,InvalidHash,OriginalMalformedEntity,UnsupportedFallback};
struct RssIssue {std::size_t channel=0,item=0;QString field;RssIssueCode code;};
struct RssResult {
  std::size_t channel=0,item=0;
  std::optional<QString> title,uid,publicationText,detailsLink,torrentLink,hash;
  std::optional<std::int64_t> size;
  std::optional<std::int32_t> seeds,peers,completed;
  bool headUnavailable=false;
};
struct RssChannel {std::size_t index=0;std::optional<QString> title;bool declaredAutoDownload=true;};
struct RssDocument {
  QByteArray source; // Owning untrusted XML: do not log as diagnostic text.
  bool atom=false;
  std::vector<RssChannel> channels;
  std::vector<RssResult> results;
  std::vector<RssIssue> issues;
};
struct RssContext {
  // Exact WebResult reconstruction prefixes, not QUrl::resolved() or a fetch
  // base. Returned URLs precede Engine.adjustLink and grant no capabilities.
  QString rootPage,basePage;
};
struct RssLimits {
  std::size_t bytes=4*1024*1024,nodes=32768,depth=64,items=4096;
  std::size_t fieldChars=65536,textChars=2*1024*1024,issues=16384;
};
// Document result parsing only. No file/network/engine/plugin/timer/selection
// effects. DTDs and entity declarations are rejected, never resolved. Missing
// facts stay optional; network-dependent HEAD answers remain Unavailable.
std::expected<RssDocument,RssError> parseRssDocument(QByteArray,RssContext={},RssLimits={});
} // namespace biglybtc::search
