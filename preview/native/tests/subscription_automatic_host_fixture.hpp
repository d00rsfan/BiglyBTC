// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "session_bridge.hpp"
#include "subscription_metainfo_fetch.hpp"
#include "subscription_rss_http_fixture.hpp"
#include "biglybtc/library.hpp"
#include "biglybtc/download_action.hpp"
#include <QDateTime>
#include <QFile>
#include <QSignalSpy>
#include <QUdpSocket>
#include <QUrl>
namespace subscription_automatic_host_fixture {
namespace f=subscription_http_fixture;namespace w=rss_fixture;namespace s=biglybtc::search;
using namespace biglybtc;
inline const QByteArray Key="result-key";
inline QByteArray bytes(const QString& path){QFile file(path);w::require(file.open(QIODevice::ReadOnly),"Cannot read automatic fixture");return file.readAll();}
inline QByteArray metainfo(bool multi=false){
  const auto info=multi?f::dictionary({{"files","l"+f::dictionary({{"length",f::integer(1)},{"path","l"+w::b("leaf")+"e"}})+"e"},{"name",w::b("Owned")},{"piece length",f::integer(16384)},{"pieces",w::b(QByteArray(20,'x'))}}):f::dictionary({{"length",f::integer(1)},{"name",w::b("Owned")},{"piece length",f::integer(16384)},{"pieces",w::b(QByteArray(20,'x'))}});
  return f::dictionary({{"future",w::b("opaque-torrent")},{"info",info}});
}
inline quint16 isolatedPort(const std::string& http){
  const int excluded=QUrl(QString::fromStdString(http)).port(-1);w::require(excluded>0,"Automatic fixture HTTP port missing");
  for(unsigned n=0;n<16;++n){w::Server tcp;QUdpSocket udp;const auto port=tcp.serverPort();if(port!=excluded&&udp.bind(QHostAddress::LocalHost,port,QAbstractSocket::DontShareAddress)){udp.close();tcp.close();return port;}}
  throw std::runtime_error("Cannot reserve automatic fixture listener");
}
struct Policy {bool start=true,defaultStopped=false,autoPause=false,hashNames=false;int minMiB=0,maxMiB=0;};
struct Paths {QString payload,torrents;};
inline Paths prepare(f::Profile& area,const std::string& metainfoUrl,Policy policy={}){
  w::require(area.lease.has_value(),"Automatic setup needs its actual lease");Paths out{area.root.filePath("payload"),area.root.filePath("named-torrents")};w::require(QDir().mkpath(out.payload)&&QDir().mkpath(out.torrents),"Cannot create automatic owned scopes");
  const auto port=isolatedPort(metainfoUrl);auto settings=storage::ConfigFile::load(*area.lease,"biglybt.config");w::require(bool(settings),"Actual automatic settings missing");
  for(const auto* key:{"Download History Enabled","TCP.Listen.Port.Enable","peercontrol.udp.public.enable","Plugin.DHT.dht.enabled","Plugin.mldht.enable","PluginInfo.mldht.enabled","Peer Source Selection Default.DHT","Peer Source Selection Default.PeerExchange","Peer Source Selection Default.Tracker","Plugin.UPnP.upnp.enable","Plugin.UPnP.natpmp.enable","IPV6 Enable Support","Delete Saved Torrent Files"}){w::require(bool(settings->document().replaceValue(key,"i0e")),"Cannot isolate automatic network policy");}
  for(const auto* key:{"TCP.Listen.Port","UDP.Listen.Port","UDP.NonData.Listen.Port"}){w::require(bool(settings->document().replaceValue(key,f::integer(port).toStdString())),"Cannot set isolated automatic port");}
  const std::vector<std::pair<std::string,qint64>> flags{{"Save Torrent Files",1},{"subscriptions.auto.start.downloads",policy.start},{"Default Start Torrents Stopped",policy.defaultStopped},{"Default Start Torrents Stopped Auto Pause",policy.autoPause},{"subscriptions.auto.dl.add.hashes",policy.hashNames},{"subscriptions.auto.start.min.mb",policy.minMiB},{"subscriptions.auto.start.max.mb",policy.maxMiB}};
  for(const auto& [key,value]:flags){w::require(bool(settings->document().replaceValue(key,f::integer(value).toStdString())),"Cannot set actual automatic policy");}
  w::require(bool(settings->document().replaceValue("Default save path",w::b(out.payload.toUtf8()).toStdString()))&&bool(settings->document().replaceValue("General_sDefaultTorrent_Directory",w::b(out.torrents.toUtf8()).toStdString()))&&bool(settings->document().replaceValue("Bind IP","9:127.0.0.1"))&&bool(settings->document().replaceValue("Enforce Bind IP","i1e"))&&bool(settings->save(*area.lease)),"Cannot save actual automatic paths/policy");
  w::require(bool(storage::ConfigFile::createMissing(*area.lease,"downloads.config","d9:downloadsle6:future3:rawe"))&&bool(storage::ConfigFile::createMissing(*area.lease,"tables.config","de")),"Cannot create automatic empty inventory");
  auto row=core::Document::parse(area.metadataRow.toStdString());w::require(bool(row),"Actual signed row missing");auto h=core::Document::parse(*core::encodeValue(*row->find("history")));w::require(bool(h),"Actual history missing");
  for(const auto& [key,value]:std::vector<std::pair<std::string,qint64>>{{"auto_dl",1},{"last_scan",QDateTime::currentMSecsSinceEpoch()},{"num_read",1},{"num_unread",0}}){w::require(bool(h->replaceValue(key,f::integer(value).toStdString())),"Cannot prepare actual automatic read history");}
  w::require(bool(row->replaceValue("history",h->serialize())),"Cannot retain actual signed metadata");area.metadataRow=QByteArray::fromStdString(row->serialize());area.configuration=f::dictionary({{"subs","l"+area.metadataRow+"e"}});w::write(area.path+"/subscriptions.config",area.configuration);
  const auto json=QJsonDocument(QJsonObject{{"n","Owned"},{"lb","1"},{"s","4294967297"},{"p","2"},{"tf","1"},{"ts","0"},{"dl",QString::fromStdString(metainfoUrl)},{"future","retain-json"}}).toJson(QJsonDocument::Compact);
  w::write(area.resultsPath,f::dictionary({{"future_results",w::b("retained")},{"results","l"+f::dictionary({{"key",w::b(Key)},{"read",f::integer(1)},{"result_json",w::b(json)},{"tz",f::integer(1)},{"future_row",w::b("retained")}})+"e"}}));area.lease.reset();return out;
}
inline app::SessionBridge::SubscriptionAcquisitionFactory factory(f::Profile& area,std::atomic<int>& made,int wrong=0){return [&area,&made,wrong](const storage::ProfileLease& lease,const std::filesystem::path& path,quint64 session,const ui::SubscriptionKey& key,const QByteArray& result,std::stop_token stop){++made;const s::SavedSubscriptionSelector selected{static_cast<std::size_t>(key.sourceIndex),key.sid.toStdString(),key.publicKey.toStdString(),key.singleton?std::optional{key.singletonKey.toStdString()}:std::nullopt,key.version};
  auto owner=wrong==1?s::SubscriptionMetainfoFetch::openFixture(lease,path,session,selected,result.toStdString(),area.endpoint(),stop):s::SubscriptionMetainfoFetch::openAutomaticFixture(lease,path,session,selected,result.toStdString(),area.endpoint(),wrong==2?std::stop_token{}:stop);return owner?std::move(*owner):std::unique_ptr<s::SubscriptionMetainfoFetch>{};};}
inline app::SessionBridge::SubscriptionFetchFactory feedFactory(f::Profile& area,std::atomic<int>& made){return [&area,&made](const storage::ProfileLease& lease,const std::filesystem::path& path,quint64 session,const ui::SubscriptionKey& key,std::stop_token stop){++made;const s::SavedSubscriptionSelector selected{static_cast<std::size_t>(key.sourceIndex),key.sid.toStdString(),key.publicKey.toStdString(),key.singleton?std::optional{key.singletonKey.toStdString()}:std::nullopt,key.version};auto owner=s::RssHttpFetch::openAutomaticSubscriptionFixture(lease,path,session,selected,area.endpoint(),stop);return owner?std::move(*owner):std::unique_ptr<s::RssHttpFetch>{};};}
inline ui::SubscriptionCatalogPresentation catalog(const QSignalSpy& spy){return qvariant_cast<ui::SubscriptionCatalogPresentation>(spy.last()[0]);}
inline ui::SubscriptionCommand command(const QSignalSpy& catalogs,ui::SubscriptionAction action=ui::SubscriptionAction::MarkUnread,quint64 offset=1){const auto c=catalog(catalogs);w::require(c&&!c->sources.empty(),"Actual automatic catalog missing");return {{c->key.session,c->key.page,c->key.request+offset,c->generation,c->sources.front().key,action},{Key},std::make_shared<std::stop_source>(),{}};}
inline bool notice(const QSignalSpy& notices,const QString& fragment){for(const auto& row:notices){if(row.at(0).toString().contains(fragment)){return true;}}return false;}
inline bool oneSnapshot(const QSignalSpy& snapshots){if(snapshots.empty()){return false;}const auto value=qvariant_cast<ui::Presentation>(snapshots.last()[0]);return value&&value->items.size()==1;}
inline QString diagnostic(const QSignalSpy& notices,int made){QString out=QString("factories=%1").arg(made);for(qsizetype i=std::max(qsizetype{0},notices.size()-3);i<notices.size();++i){out+=" | "+notices.at(i).at(0).toString().left(512);}return out;}
inline core::LibrarySnapshot library(f::Profile& area){auto document=core::Document::parse(bytes(area.path+"/downloads.config").toStdString());w::require(bool(document),"Actual automatic inventory malformed");auto value=core::readLibrary(*document);w::require(value&&value->issues.empty(),"Actual automatic inventory unavailable");return std::move(*value);}
inline void noPending(f::Profile& area){for(const auto* name:{"BiglyBTC.subscription-acquire","BiglyBTC.torrent-import",".subscriptions-native-write"}){for(const auto* suffix:{"",".saving",".bak"}){w::require(!QFile::exists(area.path+"/"+name+suffix),"Automatic pending intent remains");}}}
}
