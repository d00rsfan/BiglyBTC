// SPDX-License-Identifier: GPL-2.0-or-later
#include "tracker_status_text.hpp"
#include <QObject>
namespace biglybtc::ui {
QVariantMap presentTrackerStatus(const TrackerStatusFacts& f){
 QString text,detail=f.error.left(20000);
 if(f.paused){text=QObject::tr("Paused");}
 else if(f.live&&f.outcome==3){
  auto line=f.error.section('\n',0,0).section('\r',0,0);const auto body=line.indexOf(QStringLiteral(" | HTTP "));if(body>=0){line.truncate(body);}line=line.simplified().left(512);
  text=f.error.contains(QStringLiteral("No matching local route"))?QObject::tr("No route for tracker announce"):line.isEmpty()?QObject::tr("Tracker request failed"):QObject::tr("Error: %1").arg(line);
 }else if(f.live&&f.outcome==1){text=QObject::tr("Announcing…");detail=QObject::tr("Tracker announce is in progress");}
 else if(f.live&&f.outcome==2){text=QObject::tr("OK");if(f.peers>=0){text+=QStringLiteral(" (")+(f.peers==1?QObject::tr("1 peer"):QObject::tr("%1 peers").arg(f.peers))+')';}detail=QObject::tr("Tracker announce succeeded");}
 else if((f.scrapeSeeds||f.scrapePeers)&&!f.scrapeFailed){text=QObject::tr("Scrape OK");detail=QObject::tr("Last tracker scrape succeeded");}
 else if(f.scrapeFailed){text=QObject::tr("Scrape failed");detail=QObject::tr("The last tracker scrape failed; no detailed response is available");}
 else if(f.checking){text=QObject::tr("Waiting for verification");detail=QObject::tr("Payload verification has not finished; no announce result is available yet");}
 else if(!f.configured){text=QObject::tr("No tracker configured");}
 else if(f.queued){text=QObject::tr("Queued");detail=QObject::tr("Queued; no live announce result is available yet");}
 else if(f.live){text=QObject::tr("Not yet announced");}
 else{text=QObject::tr("Stopped");}
 if(f.scrapeSeeds||f.scrapePeers){const auto counts=QObject::tr("%1 seeds, %2 peers").arg(f.scrapeSeeds?QString::number(*f.scrapeSeeds):QStringLiteral("—"),f.scrapePeers?QString::number(*f.scrapePeers):QStringLiteral("—"));if(text==QObject::tr("Scrape OK")){text+=QStringLiteral(" (")+counts+')';}if(!detail.isEmpty()){detail+='\n';}detail+=counts;}
 if(detail.isEmpty()){detail=text;}return {{QStringLiteral("text"),text},{QStringLiteral("detail"),detail}};
}
}
