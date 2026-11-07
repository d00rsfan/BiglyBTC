// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "persistent_session.hpp"
#include "torrent_state.hpp"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QFileInfo>
#include <libtorrent/create_torrent.hpp>
#include <libtorrent/bencode.hpp>
#include <libtorrent/entry.hpp>
#include <map>
#include <stdexcept>
#include <iterator>
#include <libtorrent/file_storage.hpp>
namespace biglybtc::test::recheck {
inline QByteArray bytes(QByteArray s){return QByteArray::number(s.size())+':'+s;}
inline QByteArray dict(std::initializer_list<std::pair<QByteArray,QByteArray>> values){std::map<QByteArray,QByteArray> sorted(values.begin(),values.end());QByteArray out="d";for(const auto& [k,v]:sorted){out+=bytes(k)+v;}return out+'e';}
inline void require(bool ok){if(!ok){throw std::runtime_error("Cannot prepare private recheck fixture");}}
inline void write(const QString& name,const QByteArray& value){require(QDir().mkpath(QFileInfo(name).absolutePath()));QFile f(name);require(f.open(QIODevice::WriteOnly|QIODevice::Truncate)&&f.write(value)==value.size()&&f.flush());}
inline QByteArray read(const QString& name){QFile f(name);require(f.open(QIODevice::ReadOnly));return f.readAll();}
struct Fixture {
 QTemporaryDir root{QStringLiteral("/tmp/biglybtc-recheck-XXXXXX")};QString profile,payload,file;QByteArray full,originalPayload;core::TorrentHash hash;
 std::optional<storage::ProfileLease> lease;
 explicit Fixture(QString mode):hash(*core::TorrentHash::parse(std::string(20,'0'))){
  require(root.isValid());profile=root.path()+"/profile";payload=root.path()+"/payload";file=payload+"/checked.bin";
  require(QDir().mkpath(profile+"/active")&&QDir().mkpath(payload));full=QByteArray(mode=="compact"?2*16384+100:3*16384,'a');for(qsizetype i=0;i<full.size();++i){full[i]=static_cast<char>((i*37+19)%251);}
  QByteArray hashes;for(int i=0;i<3;++i){hashes+=QCryptographicHash::hash(full.mid(i*16384,16384),QCryptographicHash::Sha1);}
  auto info=dict({{"length",QByteArray("i")+QByteArray::number(full.size())+"e"},{"name",bytes("checked.bin")},{"piece length","i16384e"},{"pieces",bytes(hashes)}});
  if(mode=="hybrid"){
    write(file,full);libtorrent::file_storage files;files.add_file("checked.bin",full.size());
    libtorrent::create_torrent creator(files,16384);libtorrent::error_code error;
    libtorrent::set_piece_hashes(creator,payload.toStdString(),error);require(!error);
    std::string encoded;libtorrent::bencode(std::back_inserter(encoded),creator.generate());
    auto generated=core::Document::parse(encoded);require(bool(generated));const auto* raw=generated->find("info");require(raw&&raw->find("meta version")&&raw->find("pieces"));
    info=QByteArray::fromStdString(generated->originalBytes().substr(raw->begin,raw->end-raw->begin));
  }
  auto parsedHash=core::TorrentHash::parse(QCryptographicHash::hash(info,QCryptographicHash::Sha1).toStdString());require(bool(parsedHash));hash=*parsedHash;
  QByteArray attributes=dict({{"networks","l6:Publice"},{"storetypes",mode=="compact"?QByteArray("l1:Ce"):QByteArray("l1:Le")},{"opaque",bytes("state-unknown")}});
  if(mode=="linked"){const auto old=file;file=root.path()+"/linked/actual.bin";auto attrs=core::Document::parse(attributes.toStdString());require(bool(attrs));const auto link=QByteArray("0\n")+old.toUtf8()+"\n"+file.toUtf8();require(bool(attrs->replaceValue("filelinks2",("l"+bytes(link)+"e").toStdString())));attributes=QByteArray::fromStdString(attrs->serialize());}
  auto current=full;if(mode=="corrupt"){current[16384]^=0x55;}if(mode=="partial"){current.truncate(16384+100);}if(mode=="compact"){current=full.right(100);write(profile+"/active/"+QString::fromStdString(hash.hex())+"/fmfile0.dat",dict({{"version","i0e"},{"length",QByteArray("i")+QByteArray::number(full.size())+"e"}}));}
  if(mode!="missing"){write(file,current);originalPayload=current;}
  const auto resume=dict({{"data",dict({{"valid","i1e"},{"resume data",bytes(QByteArray(3,'\1'))},{"blocks","de"}})}});
  const auto state=dict({{"info",info},{"attributes",attributes},{"resume",resume},{"opaque",bytes("root-unknown")}});write(profile+"/active/"+QString::fromStdString(hash.hex())+".dat",state);
  const auto row=dict({{"torrent_hash",bytes(QByteArray(reinterpret_cast<const char*>(hash.bytes().data()),static_cast<qsizetype>(hash.bytes().size())))},{"torrent",bytes((profile+"/active/"+QString::fromStdString(hash.hex())+".dat").toUtf8())},{"save_dir",bytes(payload.toUtf8())},{"save_file",bytes("checked.bin")},{"state","i70e"},{"forceStart","i0e"},{"downloaded","i9e"},{"uploaded","i11e"},{"file_priorities",mode=="compact"?QByteArray("li0ee"):QByteArray("li1ee")},{"opaque",bytes("row-unknown")}});
  write(profile+"/downloads.config",dict({{"downloads","l"+row+"e"},{"opaque",bytes("inventory-unknown")}}));write(profile+"/biglybt.config","de");
  auto held=storage::ProfileLease::acquire(profile.toStdString());require(bool(held));lease.emplace(std::move(*held));
 }
};
}
