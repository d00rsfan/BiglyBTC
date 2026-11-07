// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "metasearch_presentation.hpp"
#include <QPointer>
#include <QWidget>
class QLineEdit;class QPushButton;class QListWidget;class QTableWidget;class QLabel;class QSpinBox;
namespace biglybtc::ui {
class MetasearchPage final:public QWidget {
 Q_OBJECT
public:
 explicit MetasearchPage(QWidget* parent=nullptr);
 ~MetasearchPage()override;
 void beginSession(quint64,quint64);void invalidateSession();void setAvailable(bool,bool preview);
 quint64 session()const noexcept{return session_;}quint64 page()const noexcept{return page_;}
 void sortResults(int,Qt::SortOrder);
 void refresh();void setCatalogPending(MetasearchCatalogKey);void refuseCatalog(MetasearchCatalogKey);void setCatalog(MetasearchCatalogPresentation);
 void setSearchPending(MetasearchQueryCommand);void refuseSearch(const MetasearchQueryCommand&);void setSearchResult(MetasearchPresentation);
 bool ownsDownload(const MetasearchDownloadKey& key,bool live=false)const{return download_&&download_->key==key&&(!live||!download_->receiver->stop_requested());}
 void setDownloadPending(MetasearchDownloadCommand);void refuseDownload(const MetasearchDownloadCommand&);void setDownloadResult(MetasearchDownloadPresentation);
signals:
 void catalogRequested(biglybtc::ui::MetasearchCatalogKey);
 void searchRequested(biglybtc::ui::MetasearchQueryCommand);
 void cancelRequested(biglybtc::ui::MetasearchQueryKey);
 void downloadRequested(biglybtc::ui::MetasearchDownloadCommand);
 void downloadCancelRequested(biglybtc::ui::MetasearchDownloadKey);
private:
 void submit();void cancelSearch();void download();void cancelDownload();void copyLink();void details();
 bool bump();void stopOwned();void controls();void filter();void render();void status(QString);
 std::optional<MetasearchResultRow> selected()const;std::vector<MetasearchEngineKey> checkedEngines()const;
 quint64 session_=0,page_=0,revision_=0,sequence_=0,nonce_=0;bool available_=true,preview_=true,catalogPending_=false,searchRunning_=false;
 std::optional<MetasearchCatalogKey> catalogRequest_;MetasearchCatalogPresentation catalog_;
 std::optional<MetasearchQueryCommand> query_;std::optional<MetasearchDownloadCommand> download_;
 std::vector<MetasearchResultRow> rows_;
 QLineEdit *queryText_,*filterText_;QPushButton *submit_,*cancel_,*refresh_,*downloadButton_,*downloadCancel_,*copy_,*details_;
 QListWidget* engines_;QTableWidget* table_;QLabel *status_,*engineStatus_,*selectionStatus_;QSpinBox *minimum_,*maximum_;
};
}
