// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "rss_document.hpp"
#include <QList>
#include <QSslCertificate>
#include <optional>

namespace biglybtc::search {
// Owning values only. This never grants an effect or accepts a source callback.
struct RssGetDescription {
  std::string literalGetUrl;
  RssContext context; // Original configured root/base; never a response final URL.
  RssLimits documentLimits;
  std::size_t responseOctets=8*1024*1024,headerBytes=16*1024;
  unsigned maximumGets=6;
  std::int64_t phaseMillis=30000,totalMillis=60000;
  // Absent is system-default trust; present is the complete decoded saved JKS.
  std::optional<QList<QSslCertificate>> trustAnchors;
};
} // namespace biglybtc::search
