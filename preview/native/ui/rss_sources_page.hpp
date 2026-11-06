// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "rss_sources_presentation.hpp"
#include <QWidget>
class QComboBox;class QPushButton;class QLabel;class QTableWidget;
namespace biglybtc::ui {
class RssSourcesPage final:public QWidget {
  Q_OBJECT
public:
  explicit RssSourcesPage(QWidget* parent=nullptr);
  ~RssSourcesPage()override;
  void beginSession(quint64 session,quint64 page);
  void invalidateSession();
  void setAvailable(bool available,bool preview);
  void refreshSources();
  void setCatalogPending(RssCatalogRequest);
  void setFetchPending(const RssFetchCommand&);
  void setCatalog(RssCatalogPresentation);
  void setFetchResult(RssFetchPresentation);
  void refuseCatalog(RssCatalogRequest);
  void refuseFetch(const RssFetchCommand&);
  quint64 session()const noexcept{return session_;}
  quint64 page()const noexcept{return page_;}
signals:
  void catalogRequested(biglybtc::ui::RssCatalogRequest);
  void fetchRequested(biglybtc::ui::RssFetchCommand);
  void cancelRequested(biglybtc::ui::RssFetchKey);
private:
  void updateControls();
  void fetch();
  void cancelFetch();
  // Local presentation revision fences synchronous widget-signal reentry.
  quint64 session_=0,page_=0,generation_=0,viewRevision_=0;
  bool available_=true,preview_=true,normal_=false,cancelling_=false,updating_=false;
  std::vector<RssSourceChoice> sources_;
  std::optional<RssCatalogRequest> catalogPending_;
  std::optional<RssFetchCommand> fetchPending_;
  QComboBox* sourcesBox_=nullptr;QPushButton* refresh_=nullptr;QPushButton* fetch_=nullptr;QPushButton* cancel_=nullptr;
  QLabel* status_=nullptr;QTableWidget* results_=nullptr;
};
} // namespace biglybtc::ui
