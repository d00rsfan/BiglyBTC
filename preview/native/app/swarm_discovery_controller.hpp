// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "swarm_discoveries_page.hpp"
#include "general_chat_service.hpp"
#include <QObject>
#include <QTimer>
namespace biglybtc::app {
class SwarmDiscoveryController final:public QObject {
 Q_OBJECT
public:
 explicit SwarmDiscoveryController(QObject* parent=nullptr);
 void setService(std::shared_ptr<GeneralChats>);
 void search(QString);void cancelSearch();
 void refresh();void discover();void lookup(QByteArray);
 void settings(services::related_content::Settings);void read(QStringList,bool);void remove(QStringList);
 void resetDeleted();void layout(QByteArray);
signals:
 void snapshotReady(biglybtc::ui::SwarmViewSnapshot);void notice(QString);
private:
 void poll();void submit(SwarmDiscoveryChanges);void accepted(bool);
 std::shared_ptr<GeneralChats> service_;QTimer timer_;std::uint64_t generation_=0,sequence_=0;
};
}
