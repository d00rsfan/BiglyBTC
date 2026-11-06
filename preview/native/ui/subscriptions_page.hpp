// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "subscriptions_presentation.hpp"
#include <QWidget>
class QComboBox;class QPushButton;class QLabel;class QTableWidget;
namespace biglybtc::ui {
class SubscriptionsPage final:public QWidget {
  Q_OBJECT
public:
  explicit SubscriptionsPage(QWidget* parent=nullptr);
  ~SubscriptionsPage()override;
  void beginSession(quint64 session,quint64 page);
  void invalidateSession();
  void setAvailable(bool available,bool preview);
  void refresh();
  void setCatalogPending(SubscriptionCatalogKey);
  void setPending(const SubscriptionCommand&);
  void setCatalog(SubscriptionCatalogPresentation);
  void setResult(SubscriptionPresentation);
  void refuseCatalog(SubscriptionCatalogKey);
  void refuseCommand(const SubscriptionCommand&);
  quint64 session()const noexcept{return session_;}
  quint64 page()const noexcept{return page_;}
signals:
  void catalogRequested(biglybtc::ui::SubscriptionCatalogKey);
  void commandRequested(biglybtc::ui::SubscriptionCommand);
  void cancelRequested(biglybtc::ui::SubscriptionCommandKey);
private:
  void updateControls();void request(SubscriptionAction);void cancel();
  quint64 session_=0,page_=0,generation_=0,revision_=0;
  bool available_=true,preview_=true,normal_=false,updating_=false,cancelling_=false;
  std::vector<SubscriptionChoice> choices_;std::optional<SubscriptionCatalogKey> catalogPending_;std::optional<SubscriptionCommand> pending_;
  QComboBox* choicesBox_=nullptr;QPushButton* refresh_=nullptr;QPushButton* check_=nullptr;QPushButton* cancel_=nullptr;QPushButton* read_=nullptr;QPushButton* unread_=nullptr;QPushButton* erase_=nullptr;
  QLabel* status_=nullptr;QTableWidget* table_=nullptr;
};
}
