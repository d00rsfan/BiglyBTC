// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "biglybtc/saved_services.hpp"
#include <QMetaType>
#include <QString>
#include <memory>
#include <optional>
#include <stop_token>
#include <vector>
namespace biglybtc::ui {
enum class PublicationKind {Shares,Tracker};
enum class PublicationSource {Primary,Backup,Saving};
struct PublicationCatalogKey {quint64 session=0,page=0,request=0;PublicationKind kind=PublicationKind::Shares;bool operator==(const PublicationCatalogKey&)const=default;};
struct PublicationRow {quint64 row=0,sourceIndex=0;bool available=false,canStart=false,canStop=false,canRemove=false;QString reason;};
struct PublicationCatalogPacket {
 PublicationCatalogKey key;quint64 generation=0;bool normal=false,needsReview=false,listenerRunning=false;
 std::optional<PublicationSource> source;std::shared_ptr<const core::SavedShares> shares;std::shared_ptr<const core::SavedHosting> hosting;
 std::vector<PublicationRow> rows;QString warning,runtime,announceUrl;
 std::optional<QString> configuredBind;std::optional<quint16> configuredPort;
};
using PublicationCatalogPresentation=std::shared_ptr<const PublicationCatalogPacket>;
enum class PublicationAction {ConfigureHttp,StartListener,StopListener,ReviewFile,ReviewDirectory,PublishShare,StopShare,RemoveShare,RecoverShares,StartHost,StopHost,RemoveHost};
struct PublicationCommandKey {quint64 session=0,page=0,request=0,catalogGeneration=0;PublicationKind kind=PublicationKind::Shares;PublicationAction action=PublicationAction::ReviewFile;bool operator==(const PublicationCommandKey&)const=default;};
struct PublicationCommand {PublicationCommandKey key;quint64 row=0;QString path,bind;quint16 port=0;std::shared_ptr<std::stop_source> receiver;};
struct PublicationConfirmation {PublicationCommandKey key;quint64 revision=0;};
struct PublicationReview {quint64 revision=0;QString title,details,confirmation;};
enum class PublicationState {Started,ReviewReady,Completed,Rejected,Cancelled,NeedsReview};
struct PublicationPacket {PublicationCommandKey key;PublicationState state=PublicationState::Rejected;QString message;std::optional<PublicationReview> review;bool committed=false,uncertain=false,backgroundAccepted=false;std::optional<quint64> completedBytes,totalBytes;};
using PublicationPresentation=std::shared_ptr<const PublicationPacket>;
}
Q_DECLARE_METATYPE(biglybtc::ui::PublicationCatalogKey)
Q_DECLARE_METATYPE(biglybtc::ui::PublicationCatalogPresentation)
Q_DECLARE_METATYPE(biglybtc::ui::PublicationCommandKey)
Q_DECLARE_METATYPE(biglybtc::ui::PublicationCommand)
Q_DECLARE_METATYPE(biglybtc::ui::PublicationConfirmation)
Q_DECLARE_METATYPE(biglybtc::ui::PublicationPresentation)
