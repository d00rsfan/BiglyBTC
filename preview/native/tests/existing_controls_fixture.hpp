// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "profile.hpp"
#include "biglybtc/bencode.hpp"
#include "biglybtc/library.hpp"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTcpServer>
#include <QUdpSocket>
#include <stdexcept>
#include <utility>
#include <vector>
namespace existing_controls_fixture {
using namespace biglybtc;
inline void require(bool value,const char* text){if(!value){throw std::runtime_error(text);}}
inline std::string b(std::string_view value){return std::to_string(value.size())+":"+std::string(value);}
inline void write(const QString& path,const QByteArray& bytes){require(QDir().mkpath(QFileInfo(path).absolutePath()),"Cannot create owned fixture directory");QFile f(path);require(f.open(QIODevice::WriteOnly|QIODevice::Truncate)&&f.write(bytes)==bytes.size()&&f.flush(),"Cannot write actual fixture file");}
inline QByteArray read(const QString& path){QFile f(path);require(f.open(QIODevice::ReadOnly),"Cannot read actual fixture file");return f.readAll();}
struct Profile {
 QTemporaryDir root{QStringLiteral("/tmp/biglybtc-existing-controls-XXXXXX")};QString path,payload,state;QByteArray original;core::TorrentHash hash;bool paused=false;
 Profile(bool corrupt,bool missing,bool pause):paused(pause){
  require(root.isValid(),"Cannot create private existing-library fixture");path=root.path()+"/profile";payload=root.path()+"/data/sample.bin";original=QByteArray(65536,'\0');for(qsizetype i=0;i<original.size();++i){original[i]=static_cast<char>((i*31+7)%251);}auto bytes=original;if(corrupt){bytes[0]=static_cast<char>(bytes[0]^1);}write(payload,bytes);if(missing){require(QFile::remove(payload),"Cannot prepare actual missing leaf");}
  auto info=core::Document::parse("de");require(bool(info),"Cannot prepare fixture metainfo");std::string pieces;for(qsizetype i=0;i<original.size();i+=16384){pieces+=QCryptographicHash::hash(original.mid(i,16384),QCryptographicHash::Sha1).toStdString();}require(bool(info->replaceValue("length","i65536e"))&&bool(info->replaceValue("name","10:sample.bin"))&&bool(info->replaceValue("piece length","i16384e"))&&bool(info->replaceValue("pieces",b(pieces))),"Cannot prepare genuine v1 info");const auto rawInfo=info->serialize();const auto digest=QCryptographicHash::hash(QByteArray::fromStdString(rawInfo),QCryptographicHash::Sha1);auto parsed=core::TorrentHash::parse(std::string_view(digest.constData(),static_cast<std::size_t>(digest.size())));require(bool(parsed),"Cannot derive actual fixture hash");hash=*parsed;state=path+"/active/"+QString::fromStdString(hash.hex())+".dat";
  auto metadata=core::Document::parse("de");require(bool(metadata)&&bool(metadata->replaceValue("attributes","d8:networksl6:Publice10:storetypesl1:Le14:unknown.future3:rawe"))&&bool(metadata->replaceValue("info",rawInfo))&&bool(metadata->replaceValue("unknown.future","3:raw")),"Cannot construct real ordinary state");write(state,QByteArray::fromStdString(metadata->serialize()));
  auto row=core::Document::parse("de");require(bool(row),"Cannot construct ordinary existing row");for(const auto& [key,value]:std::vector<std::pair<std::string,std::string>>{{"torrent_hash",b(digest.toStdString())},{"torrent",b(state.toStdString())},{"save_dir",b((root.path()+"/data").toStdString())},{"save_file","10:sample.bin"},{"persistent","i1e"},{"state","i70e"},{"position","i1e"},{"forceStart","i0e"},{"file_priorities","li1ee"},{"unknown.future","l3:rawi7ee"}}){require(bool(row->replaceValue(key,value)),"Cannot populate ordinary existing row");}
  auto inventory=core::Document::parse("de");require(bool(inventory)&&bool(inventory->replaceValue("downloads","l"+row->serialize()+"e"))&&bool(inventory->replaceValue("unknown.future","3:raw")),"Cannot populate existing inventory");if(paused){require(bool(inventory->replaceValue("pause_data","ld4:hash"+b(digest.toStdString())+"5:forcei1e5:owneri1eee")),"Cannot preserve genuine paused force intent");}write(path+"/downloads.config",QByteArray::fromStdString(inventory->serialize()));write(path+"/tables.config","de");write(path+"/biglybt.config","de");
  auto lease=storage::ProfileLease::acquire(path.toStdString());require(bool(lease),"Cannot hold fixture profile");auto settings=storage::ConfigFile::load(*lease,"biglybt.config");require(bool(settings),"Cannot load actual fixture settings");QTcpServer tcp;require(tcp.listen(QHostAddress::LocalHost,0),"Cannot select private fixture port");const auto port=tcp.serverPort();for(const auto* key:{"TCP.Listen.Port","UDP.Listen.Port","UDP.NonData.Listen.Port"}){require(bool(settings->document().replaceValue(key,"i"+std::to_string(port)+"e")),"Cannot set private fixture port");}for(const auto* key:{"Download History Enabled","TCP.Listen.Port.Enable","peercontrol.udp.public.enable","Plugin.DHT.dht.enabled","Plugin.mldht.enable","PluginInfo.mldht.enabled","Peer Source Selection Default.DHT","Peer Source Selection Default.PeerExchange","Peer Source Selection Default.Tracker","Plugin.UPnP.upnp.enable","Plugin.UPnP.natpmp.enable","IPV6 Enable Support"}){require(bool(settings->document().replaceValue(key,"i0e")),"Cannot isolate normal existing fixture");}require(bool(settings->document().replaceValue("Bind IP","9:127.0.0.1"))&&bool(settings->document().replaceValue("Enforce Bind IP","i1e"))&&bool(settings->save(*lease)),"Cannot save private existing settings");
 }
};
}
