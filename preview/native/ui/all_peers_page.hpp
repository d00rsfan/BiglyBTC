// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "all_peers_presentation.hpp"
#include <QAbstractTableModel>
#include <QWidget>
class QLabel;class QLineEdit;class QSortFilterProxyModel;class QTableView;class QToolButton;
namespace biglybtc::ui {
class AllPeersModel final:public QAbstractTableModel {
 Q_OBJECT
public:
 enum Role {SortRole=Qt::UserRole+1,BinsRole,ProgressRole};
 explicit AllPeersModel(QObject* parent=nullptr);
 int rowCount(const QModelIndex& parent={})const override;int columnCount(const QModelIndex& parent={})const override;
 QVariant data(const QModelIndex&,int role=Qt::DisplayRole)const override;
 QVariant headerData(int,Qt::Orientation,int role=Qt::DisplayRole)const override;
 bool apply(PeerPresentation);void beginSession(quint64);
 const PeerRow* row(int)const;static QString columnId(int);static int columnFor(QString);
 PeerPresentation packet()const{return packet_;}
private:quint64 session_=0;PeerPresentation packet_;
};
class AllPeersPage final:public QWidget {
 Q_OBJECT
public:
 explicit AllPeersPage(quint64 session,QWidget* parent=nullptr);
 void setSnapshot(PeerPresentation);void invalidateSession();void refresh();
 AllPeersModel* model()const{return model_;}QTableView* table()const{return table_;}
signals:void openTorrent(QString);void refreshRequested();
private:
 void applyLayout(const core::SavedTableLayout&);void updateSummary();void popup(const QPoint&);
 quint64 session_=0;bool layoutApplied_=false;AllPeersModel* model_;QSortFilterProxyModel* proxy_;QTableView* table_;QLabel* status_;QLineEdit* filter_;QToolButton* columns_;
};
}
