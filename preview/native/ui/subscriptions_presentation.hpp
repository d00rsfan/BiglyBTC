// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QByteArray>
#include <QString>
#include <QMetaType>
#include <memory>
#include <optional>
#include <stop_token>
#include <vector>
namespace biglybtc::ui {
struct SubscriptionKey {quint64 sourceIndex=0;QByteArray sid,publicKey,singletonKey;qint64 version=0;bool singleton=false;bool operator==(const SubscriptionKey&)const=default;};
struct SubscriptionCatalogKey {quint64 session=0,page=0,request=0;bool operator==(const SubscriptionCatalogKey&)const=default;};
struct SubscriptionChoice {SubscriptionKey key;QString name,status;bool canCheck=false;};
struct SubscriptionCatalogPacket {SubscriptionCatalogKey key;quint64 generation=0;bool normalNetwork=false,truncated=false;std::vector<SubscriptionChoice> sources;QString failure;};
using SubscriptionCatalogPresentation=std::shared_ptr<const SubscriptionCatalogPacket>;
enum class SubscriptionAction {ReadResults,Check,MarkRead,MarkUnread,Delete,AcquireStopped};
struct SubscriptionCommandKey {quint64 session=0,page=0,request=0,generation=0;SubscriptionKey target;SubscriptionAction action=SubscriptionAction::ReadResults;bool operator==(const SubscriptionCommandKey&)const=default;};
struct SubscriptionCommand {SubscriptionCommandKey key;std::vector<QByteArray> resultKeys;std::shared_ptr<std::stop_source> receiver;QString destination{};};
enum class SubscriptionState {Started,Ready,Rejected,Cancelled};
struct SubscriptionResultRow {QByteArray key;bool read=false;QString title,size,seeds,peers,publication,link;};
struct SubscriptionPacket {SubscriptionCommandKey key;SubscriptionState state=SubscriptionState::Rejected;bool committed=false,truncated=false;std::vector<SubscriptionResultRow> rows;QString message;};
using SubscriptionPresentation=std::shared_ptr<const SubscriptionPacket>;
inline constexpr std::size_t MaximumSubscriptionRows=256;
}
Q_DECLARE_METATYPE(biglybtc::ui::SubscriptionKey)
Q_DECLARE_METATYPE(biglybtc::ui::SubscriptionCatalogKey)
Q_DECLARE_METATYPE(biglybtc::ui::SubscriptionCatalogPresentation)
Q_DECLARE_METATYPE(biglybtc::ui::SubscriptionCommandKey)
Q_DECLARE_METATYPE(biglybtc::ui::SubscriptionCommand)
Q_DECLARE_METATYPE(biglybtc::ui::SubscriptionPresentation)
