// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "../app/download_history_commands.hpp"
#include <QWidget>
#include <optional>
class QLabel;
class QPushButton;
class QTableWidget;
namespace biglybtc::ui {
class DownloadHistoryPage final:public QWidget {
  Q_OBJECT
public:
  explicit DownloadHistoryPage(QWidget* parent=nullptr);
  void beginSession(quint64);
  void invalidateSession();
  void setAvailable(bool);
  void setPending(DownloadHistoryCommand);
  void setResult(DownloadHistoryCommandPresentation);
  void refuse(const DownloadHistoryCommand&);
  void reload();
  quint64 session()const noexcept{return session_;}
signals:
  void commandRequested(biglybtc::ui::DownloadHistoryCommand);
private:
  struct Key {DownloadHistorySide side; qint64 uid;bool operator==(const Key&)const=default;};
  std::optional<Key> selected()const;
  void updateButtons();void erase();void clear();void reset();void intent(DownloadHistoryCommand);
  QLabel* status_;QTableWidget* table_;QPushButton* reload_;QPushButton* erase_;QPushButton* clear_;QPushButton* reset_;
  quint64 session_=0,generation_=0;bool available_=false,writable_=false,resetAvailable_=false;
  std::optional<DownloadHistoryCommand> pending_;
  DownloadHistoryCommandPresentation shown_;
};
} // namespace biglybtc::ui
