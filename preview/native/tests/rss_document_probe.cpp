// SPDX-License-Identifier: GPL-2.0-or-later
// Excluded offline value oracle. This is not a search/import service.
#include "rss_document.hpp"
#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <iostream>
int main(int argc,char** argv){
  QCoreApplication app(argc,argv);if(argc!=5){return 2;}
  QFile input(QString::fromLocal8Bit(argv[1]));
  if(!input.open(QIODevice::ReadOnly)||input.size()<0||input.size()>4*1024*1024){return 3;}
  auto value=biglybtc::search::parseRssDocument(input.readAll(),{QString::fromUtf8(argv[3]),QString::fromUtf8(argv[4])});
  QJsonObject root;root["status"]=value?"PARSED_VALUES_ONLY":"REFUSED";
  if(!value){root["error"]=static_cast<int>(value.error());}
  else{
    root["atom"]=value->atom;QJsonArray channels,rows,issues;
    for(const auto& c:value->channels){QJsonObject out;out["index"]=static_cast<qint64>(c.index);if(c.title){out["title"]=*c.title;}out["declaredAutoDownload"]=c.declaredAutoDownload;channels.push_back(out);}
    for(const auto& r:value->results){
      QJsonObject out;out["channel"]=static_cast<qint64>(r.channel);out["item"]=static_cast<qint64>(r.item);
      const auto text=[&](const char* name,const std::optional<QString>& field){if(field){out[QLatin1StringView(name)]=*field;}};
      text("title",r.title);text("uid",r.uid);text("publicationText",r.publicationText);text("detailsLink",r.detailsLink);text("torrentLink",r.torrentLink);text("hash",r.hash);
      if(r.size){out["size"]=QString::number(*r.size);}if(r.seeds){out["seeds"]=*r.seeds;}if(r.peers){out["peers"]=*r.peers;}if(r.completed){out["completed"]=*r.completed;}
      out["headUnavailable"]=r.headUnavailable;rows.push_back(out);
    }
    for(const auto& i:value->issues){QJsonObject out;out["channel"]=static_cast<qint64>(i.channel);out["item"]=static_cast<qint64>(i.item);out["field"]=i.field;out["code"]=static_cast<int>(i.code);issues.push_back(out);}
    root["channels"]=channels;root["results"]=rows;root["issues"]=issues;
  }
  QFile output(QString::fromLocal8Bit(argv[2]));const auto bytes=QJsonDocument(root).toJson();
  if(!output.open(QIODevice::WriteOnly|QIODevice::NewOnly)||output.write(bytes)!=bytes.size()||!output.flush()){return 4;}
  std::cout<<"offline RSS values only\n";return value?0:1;
}
