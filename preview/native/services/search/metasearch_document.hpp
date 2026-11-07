// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "metasearch_values.hpp"
#include "rss_document.hpp"
#include <QByteArray>
namespace biglybtc::search {
enum class MetasearchDocumentError {Encoding,Format,Unsupported,Limit,Memory};
struct MetasearchDocument {
  std::vector<MetasearchRowValues> rows;
  bool partial=false;
};
// Bounded values only. Input context is the actual configured root/base,
// deliberately independent of the redirected response URL.
std::expected<MetasearchDocument,MetasearchDocumentError> parseMetasearchDocument(
    const MetasearchRecipe&,const QByteArray&,const RssContext&);
} // namespace biglybtc::search
