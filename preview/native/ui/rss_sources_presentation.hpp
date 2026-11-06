// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QByteArray>
#include <QMetaType>
#include <QString>
#include <memory>
#include <optional>
#include <stop_token>
#include <vector>

namespace biglybtc::ui {
struct RssSourceKey {quint64 sourceIndex=0;qint64 id=0;QByteArray uid;bool operator==(const RssSourceKey&)const=default;};
struct RssCatalogRequest {quint64 session=0,page=0,request=0;bool operator==(const RssCatalogRequest&)const=default;};
struct RssSourceChoice {RssSourceKey key;QString name,url;bool selectable=false;};
struct RssCatalogPacket {
  RssCatalogRequest key;quint64 generation=0;bool normalNetwork=false,truncated=false;
  std::vector<RssSourceChoice> sources;QString failure;
};
using RssCatalogPresentation=std::shared_ptr<const RssCatalogPacket>;
struct RssFetchKey {
  quint64 session=0,page=0,request=0,catalogGeneration=0;RssSourceKey target;
  bool operator==(const RssFetchKey&)const=default;
};
struct RssFetchCommand {
  RssFetchKey key;
  // Revocation only. Created before any signal; no Host pointer or authority.
  std::shared_ptr<std::stop_source> receiver;
};
enum class RssFetchState {Started,Ready,Rejected,Cancelled};
struct RssResultRow {QString title,size,seeds,peers,publication,link;};
struct RssFetchPacket {
  RssFetchKey key;RssFetchState state=RssFetchState::Rejected;
  std::vector<RssResultRow> rows;bool truncated=false;QString message;
};
using RssFetchPresentation=std::shared_ptr<const RssFetchPacket>;
inline constexpr std::size_t MaximumRssSourceRows=256,MaximumRssResultRows=256,MaximumRssDisplayChars=2048;
} // namespace biglybtc::ui
Q_DECLARE_METATYPE(biglybtc::ui::RssCatalogRequest)
Q_DECLARE_METATYPE(biglybtc::ui::RssCatalogPresentation)
Q_DECLARE_METATYPE(biglybtc::ui::RssFetchKey)
Q_DECLARE_METATYPE(biglybtc::ui::RssFetchCommand)
Q_DECLARE_METATYPE(biglybtc::ui::RssFetchPresentation)
