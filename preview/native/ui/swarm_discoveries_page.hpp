// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "related_content.hpp"
#include "search.hpp"
#include <QWidget>
#include <memory>
class QLabel;class QLineEdit;class QCheckBox;class QTableView;class QStandardItemModel;
class QSortFilterProxyModel;class QTimer;class QPushButton;
namespace biglybtc::ui {
namespace rcm=services::related_content;
struct SwarmViewSnapshot {
 std::uint64_t generation=0,sequence=0;
 std::shared_ptr<const rcm::Snapshot> profile;
 QString status;
 std::uint64_t lookups=0,publications=0,completed=0;
 std::size_t active=0,pending=0;
 std::shared_ptr<const rcm::SearchProgress> search;
};
class SwarmDiscoveriesPage final:public QWidget {
 Q_OBJECT
public:
 explicit SwarmDiscoveriesPage(QWidget* parent=nullptr);
 void setSnapshot(SwarmViewSnapshot);
 void filterRelated(QByteArray hash);
signals:
 void searchRequested(QString);void cancelSearchRequested();
 void refreshRequested();void discoverRequested();void lookupRequested(QByteArray);
 void settingsRequested(biglybtc::services::related_content::Settings);
 void readRequested(QStringList,bool unread);void removeRequested(QStringList);
 void resetDeletedRequested();void layoutRequested(QByteArray);
private:
 void rebuild();void updateFilter();void configure();void contextMenu(QPoint);
 void applyLayout();void saveLayout();void markSelected(bool unread);void copyMagnet();
 QStringList selectedKeys()const;const rcm::Item* selectedItem()const;
 SwarmViewSnapshot value_;QByteArray related_;std::shared_ptr<const rcm::Snapshot> display_;
 bool searchMode_=false;std::uint64_t lastSearchId_=0;
 QLineEdit* searchTerm_;QPushButton* searchButton_;QPushButton* cancelSearch_;QLabel* searchStatus_;
 QLabel* status_;QLabel* count_;QLineEdit* filter_;QCheckBox* unread_;QCheckBox* allSources_;
 QTableView* table_;QStandardItemModel* model_;QSortFilterProxyModel* proxy_;QTimer* layoutTimer_;
 QPushButton* discover_;QPushButton* settings_;bool applying_=false,layoutApplied_=false;
};
}
