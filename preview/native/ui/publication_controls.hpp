// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "publication_presentation.hpp"
#include <QWidget>
#include <QPointer>
class QTableView;class QLineEdit;class QSpinBox;class QLabel;class QPushButton;
namespace biglybtc::ui {
class PublicationControls final:public QWidget {
 Q_OBJECT
public:
 PublicationControls(PublicationKind,QTableView*,QWidget* parent=nullptr);~PublicationControls()override;
 void beginSession(quint64,quint64);void setAvailable(bool,bool);void refresh();
 void setCatalogPending(PublicationCatalogKey);void setCatalog(PublicationCatalogPresentation);void refuseCatalog(PublicationCatalogKey);
 void setPending(PublicationCommand);void setResult(PublicationPresentation);void refuse(const PublicationCommand&);
 bool owns(const PublicationCommandKey&,bool live=false)const;quint64 session()const{return session_;}quint64 page()const{return page_;}PublicationKind kind()const{return kind_;}
 void invalidate();
signals:
 void catalogRequested(biglybtc::ui::PublicationCatalogKey);
 void commandRequested(biglybtc::ui::PublicationCommand);
 void cancelRequested(biglybtc::ui::PublicationCommandKey);
private:
 void issue(PublicationAction);void cancel();void controls();void stopPending(bool notify);std::optional<PublicationRow> selected()const;
 PublicationKind kind_;QPointer<QTableView> table_;QMetaObject::Connection selection_,reset_;
 quint64 session_=0,page_=0,revision_=0;bool available_=false,preview_=true,catalogPending_=false,refreshWanted_=false;
 std::optional<PublicationCatalogKey> catalogKey_;PublicationCatalogPresentation catalog_;std::optional<PublicationCommand> command_;
 QLineEdit *path_=nullptr,*bind_=nullptr;QSpinBox* port_=nullptr;QLabel *status_,*runtime_;
 QPushButton *refresh_,*cancel_,*file_=nullptr,*directory_=nullptr,*publish_=nullptr,*stopShare_=nullptr,*removeShare_=nullptr,*recover_=nullptr,*configure_=nullptr,*start_=nullptr,*stop_=nullptr,*startRow_=nullptr,*stopRow_=nullptr,*removeRow_=nullptr;
};
}
