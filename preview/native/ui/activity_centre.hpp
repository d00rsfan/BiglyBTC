// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "chat_notifications.hpp"
#include <QWidget>
#include <QVector>
#include <QDateTime>
class QTableWidget;class QLabel;
namespace biglybtc::ui {
struct ActivityRefView {QString id;QByteArray stamp;bool operator==(const ActivityRefView&)const=default;};
struct ActivityRowView {ActivityRefView source;QString text,key;QDateTime time;bool read=false,viewed=false,canView=false;};
struct ActivityView {quint64 session=0;QVector<ActivityRowView> rows;QString status;bool writable=false;ChatNotificationSettings settings;};
class ActivityCentre final:public QWidget {
 Q_OBJECT
public:
 explicit ActivityCentre(QWidget* parent=nullptr);void setSnapshot(ActivityView);
signals:
 void command(quint64,QString,QVector<biglybtc::ui::ActivityRefView>);
 void viewRequested(quint64,QString,biglybtc::ui::ActivityRefView);
 void settingsRequested(quint64,biglybtc::ui::ChatNotificationSettings,biglybtc::ui::ChatNotificationSettings);
 void soundRequested(QString);
private:
 QVector<ActivityRefView> selected()const;void change(QString);void view();
 ActivityView value_;QTableWidget* table_;QLabel* status_;
};
}
Q_DECLARE_METATYPE(biglybtc::ui::ActivityView)
