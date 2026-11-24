// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QStringList>
#include <QWidget>
#include <memory>
#include <vector>
class QLabel;
class QPushButton;
class QTreeWidget;

namespace biglybtc::ui {
struct AllTrackerOriginView {
  QString name;
  unsigned torrents=0;
};
struct AllTrackersViewResult {
  quint64 session=0,request=0;
  std::vector<AllTrackerOriginView> origins;
  QStringList selected,members;
  QString failure;
  bool canSelect=false,selectionActive=false;
};
using AllTrackersPresentation=std::shared_ptr<const AllTrackersViewResult>;

class AllTrackersView final:public QWidget {
  Q_OBJECT
public:
  explicit AllTrackersView(QWidget* parent=nullptr);
  void begin(quint64 session);
  void requestSent(quint64 request);
  void present(AllTrackersPresentation);
  void available(bool);
signals:
  void loadRequested(bool apply,QStringList origins);
private:
  void updateControls();
  quint64 session_=0,request_=0;
  bool available_=false,pending_=false,canSelect_=false;
  QLabel* status_;
  QPushButton* refresh_;
  QPushButton* apply_;
  QTreeWidget* origins_;
  QTreeWidget* torrents_;
};
}
Q_DECLARE_METATYPE(biglybtc::ui::AllTrackersPresentation)
