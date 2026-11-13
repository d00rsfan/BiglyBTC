// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "torrent_detail_presentation.hpp"
#include "chat_page.hpp"
#include <QObject>
#include <QPointer>
#include <QStringList>
class QTabWidget;class QTableView;class QLabel;class QSpinBox;class QPushButton;
namespace biglybtc::ui {
void applyTorrentTableLayout(QTableView*,const core::SavedTableLayout&,const QStringList&);
class AllPeersPage;class InspectionTableModel;class TorrentPieceMap;class TorrentActivityPlot;class TorrentSwarmPlot;
class TorrentDetailPages final:public QObject {
 Q_OBJECT
public:
 explicit TorrentDetailPages(QTabWidget*);
 engine::InspectionSection section()const;int firstPiece()const;
 void beginInterest(const TorrentDetailInterest&);bool apply(TorrentDetailPresentation);
 void setDownloadContext(quint64,QString);void setDownloadChat(quint64,QString,ChatViewSnapshot);
 int chatIndex()const;bool chatVisible()const;QString readingRoom()const;ChatPage* chatPage()const{return chat_;}
 void setTags(const QStringList&);void setOptionsWritable(bool);void optionsFinished(QString failure);void trackerFinished(QString failure);
signals:
 void downloadChatRequested(quint64,QString);void downloadChatObservationChanged(quint64,QString);
 void interestChanged();void tagsRequested();void trackerRequested(bool scrape);void optionsRequested(bool rates,int first,int second);
private:
 ChatPage* chat_=nullptr;quint64 downloadSession_=0;QString downloadHash_;bool chatRequested_=false,chatObserved_=false;
 QTabWidget* tabs_;TorrentDetailInterest interest_;quint64 sequence_=0,admission_=0,epoch_=0;
 QLabel* status_;AllPeersPage* peers_;InspectionTableModel *general_,*trackers_,*pieces_,*blocks_,*information_,*privacy_,*transfers_,*tagsModel_,*console_;
 TorrentPieceMap* map_;TorrentActivityPlot* activity_;TorrentSwarmPlot* swarm_;
 QSpinBox *first_,*download_,*upload_,*connections_,*slots_;QPushButton* applyOptions_;QPushButton* applyConnections_;QPushButton* reannounce_;QPushButton* scrape_;
 quint64 layoutSession_=0;std::array<bool,2> layoutApplied_{};
 bool optionsDirty_=false;QString observedHash_;
};
}
