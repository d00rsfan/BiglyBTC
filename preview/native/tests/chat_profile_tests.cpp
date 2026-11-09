// SPDX-License-Identifier: GPL-2.0-or-later
#include "chat_profile.hpp"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include <iostream>
#include <stdexcept>
using namespace biglybtc;
namespace {
void need(bool ok,const char* text){if(!ok){throw std::runtime_error(text);}}
template<class T>T take(std::expected<T,storage::Error> value){if(!value){throw std::runtime_error(value.error().message);}return std::move(*value);}
void done(std::expected<void,storage::Error> value){if(!value){throw std::runtime_error(value.error().message);}}
std::string read(const QString& path){QFile file(path);need(file.open(QIODevice::ReadOnly),"Read generated file");return file.readAll().toStdString();}
void write(const QString& path,const std::string& bytes){QFile file(path);need(file.open(QIODevice::WriteOnly|QIODevice::Truncate)&&file.write(bytes.data(),static_cast<qint64>(bytes.size()))==static_cast<qint64>(bytes.size()),"Write generated file");file.close();}
}
int main(int argc,char** argv){QCoreApplication app(argc,argv);try{
 QTemporaryDir dir;need(dir.isValid(),"Temporary profile");auto lease=take(storage::ProfileLease::acquire(dir.path().toStdString()));
 auto cfg=take(storage::ConfigFile::createMissing(lease,"biglybt.config","d7:unknowni42e24:azbuddy.chat.shared_nick9:Generatede"));(void)cfg;
 auto profile=take(app::ChatProfile::open(lease));need(profile->lease().sharesOwnership(lease),"Chat must retain the same lease");
 need(app::ChatProfile::configKey("room")=="core.crypto.msgsync.Public.726F6F6D"&&app::ChatProfile::optionKey("room")=="Public:OJXW63I"&&app::ChatProfile::historyLeaf("room")=="OJXW63Ip.dat","Original key mapping differs");
 need(!profile->identity("not created",false),"Missing identity silently created");auto identity=take(profile->identity("Generated general room",true));const auto uid=identity.uid;const std::string pub(identity.signer.publicKey());
 auto reopened=take(app::ChatProfile::open(lease));auto retained=take(reopened->identity("Generated general room",false));need(uid==retained.uid&&pub==retained.signer.publicKey(),"Saved signing identity rotated");
 auto options=retained.settings;options.favourite=true;options.saveHistory=true;done(profile->saveOptions(options));auto rooms=take(profile->rooms());need(rooms.size()==1&&rooms[0].favourite&&rooms[0].saveHistory&&rooms[0].nickname=="Generated","Original room options lost");
 auto actual=take(storage::ConfigFile::load(lease,"biglybt.config"));need(actual.document().find("unknown")->integer==42,"Unrelated configuration changed");
 auto empty=take(profile->history(options.key));need(!empty,"Unexpected history");
 const auto contact=std::string("d1:a4:")+std::string("\x7f\0\0\1",4)+"1:pi43123e1:vi55ee";
 auto room=services::msgsync::Room::open(options.key,identity.uid,std::move(identity.signer),contact);need(bool(room),"Open generated room");need(bool(room->send("Generated message","Generated",100000)),"Create signed history");auto history=room->exportHistory(100000);need(bool(history),"Export history");done(profile->saveHistory(options.key,*history));
 const auto path=dir.path()+"/plugins/azmsgsync/save/"+QString::fromStdString(app::ChatProfile::historyLeaf(options.key));need(read(path)==*history,"History publication differs");
 auto document=core::Document::parse(read(path));need(bool(document)&&bool(document->replaceValue("future-root","i9e")),"Extend generated history");
 auto node=core::Document::parse("de");need(bool(node),"Node fixture");const auto* originalNodes=document->find("nodes");auto nodes=core::Document::parse(core::encodeValue(*originalNodes).value());need(bool(nodes),"Nodes fixture");node=core::Document::parse(core::encodeValue(*nodes->find("0")).value());need(bool(node->replaceValue("future-node","i10e"))&&bool(nodes->replaceValue("0",node->serialize()))&&bool(document->replaceValue("nodes",nodes->serialize())),"Extend node");
 auto row=core::Document::parse(core::encodeValue(document->find("messages")->children[0]).value());need(bool(row)&&bool(row->replaceValue("future-message","i11e"))&&bool(document->replaceValue("messages","l"+row->serialize()+"e")),"Extend signed record");write(path,document->serialize());
 need(!profile->saveHistory(options.key,*history),"Foreign history update overwritten");take(reopened->history(options.key));auto updated=room->exportHistory(103000);need(bool(updated),"Export aged history");done(reopened->saveHistory(options.key,*updated));
 auto merged=core::Document::parse(read(path));need(merged&&merged->find("future-root")->integer==9&&merged->find("nodes")->find("0")->find("future-node")->integer==10&&merged->find("messages")->children[0].find("future-message")->integer==11,"History extensions discarded");
 write(path+".saving","retained interrupted write");const auto before=read(path);need(!reopened->saveHistory(options.key,*updated)&&read(path)==before,"Interrupted history overwritten");need(QFile::remove(path+".saving"),"Remove owned interrupted fixture");
 auto corrupt=take(storage::ConfigFile::load(lease,"biglybt.config"));need(bool(corrupt.document().replaceValue(app::ChatProfile::configKey(options.key),"de")),"Corrupt owned key fixture");done(corrupt.save(lease));const auto configBefore=read(dir.path()+"/biglybt.config");need(!profile->identity(options.key,true)&&read(dir.path()+"/biglybt.config")==configBefore,"Unreadable original signing identity silently replaced");
 const auto malicious=dir.path()+"/plugins/azmsgsync/save/"+QString::fromStdString(app::ChatProfile::historyLeaf("Generated symlink"));need(QFile::link(path,malicious),"Create owned symlink");need(!profile->history("Generated symlink"),"History symlink followed");
 std::cout<<"PASS original chat profile: stable identity, same lease, catalogue, options, history, unknown fields, CAS, interrupted writes, corrupt key refusal, symlink refusal\n";return 0;
 }catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
