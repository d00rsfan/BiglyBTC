// SPDX-License-Identifier: GPL-2.0-or-later
#include "chat_profile.hpp"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <iostream>
#include <stdexcept>
using namespace biglybtc;
namespace msg=services::msgsync;namespace crypto=plugins::msgsync;
namespace {
void need(bool value,const char* why){if(!value){throw std::runtime_error(why);}}
template<class T,class E>T take(std::expected<T,E> value){need(bool(value),"Expected generated chat result");return std::move(*value);}
std::string b(std::string_view value){return std::to_string(value.size())+":"+std::string(value);}
std::string base32(std::string_view input){constexpr std::string_view chars="ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";std::string out;unsigned bits=0,value=0;for(unsigned char c:input){value=(value<<8)|c;bits+=8;while(bits>=5){bits-=5;out+=chars[(value>>bits)&31];}}if(bits){out+=chars[(value<<(5-bits))&31];}return out;}
std::span<const std::uint8_t> span(std::string_view value){return {reinterpret_cast<const std::uint8_t*>(value.data()),value.size()};}
msg::Room room(std::string key,const crypto::GeneratedMessageIdentity& identity,std::uint8_t number){msg::Uid uid{};uid[0]=number;return take(msg::Room::open(std::move(key),uid,take(crypto::MessageSigner::open(identity.publicKey,identity.privateKey)),"de"));}
void policy(){
 auto owner=take(crypto::generateMessageIdentity()),reader=take(crypto::generateMessageIdentity()),rogue=take(crypto::generateMessageIdentity());
 const std::string key="Generated announcement [pk="+base32(owner.publicKey)+"&ro=1]";
 const auto parsed=take(msg::roomPolicy(key));need(parsed.readOnly&&parsed.manager==owner.publicKey,"Original ampersand managed suffix");
 auto author=room(key,owner,1),viewer=room(key,reader,2);need(!author.readOnly()&&viewer.readOnly(),"Managed author/read-only role");need(!viewer.send("forbidden","Reader",100000),"Read-only composer allowed");take(author.send("Owner post","Owner",100000));
 msg::NodeHint hint;hint.uid[0]=3;hint.encodedContact="d1:a4:"+std::string("\x7f\0\0\x01",4)+"1:pi54321e1:vi55ee";
 const auto answer=take(author.answerDetailed(take(viewer.request(100000)),100000,hint));need(answer.requester==viewer.uid()&&!answer.self,"Actual requester UID lost");auto received=take(viewer.receive(answer.envelope,100000));need(received.added==1&&received.hint&&received.hint->uid==hint.uid&&received.hint->encodedContact==hint.encodedContact,"Unsigned live-node hint lost");
 auto signer=take(crypto::MessageSigner::open(rogue.publicKey,rogue.privateKey));msg::Uid rogueUid{};rogueUid[0]=3;auto forged=take(msg::signMessage(signer,rogueUid,"d3:msg5:rogue4:nick1:Re"));forged.publicKey=rogue.publicKey;forged.encodedContact="de";msg::SyncReply reply;reply.messages.push_back(forged);
 const auto keys=take(crypto::deriveChannelKeys(span(key)));const auto encoded=take(msg::encodeSyncReply(reply));const auto envelope=take(crypto::encrypt(crypto::Kind::General,keys.general,span(encoded)));
 auto rejected=take(viewer.receive(envelope,100000));need(rejected.rejected==1&&viewer.retainedMessages()==1,"Non-owner signed post entered managed read-only room");
 auto invalid=take(core::Document::parse(encoded));need(bool(invalid.replaceValue("n","3:bad")),"Malformed optional hint fixture");auto decoded=take(msg::decodeSyncReply(invalid.serialize()));need(decoded.messages.size()==1&&!decoded.hint,"Malformed unsigned hint suppressed valid messages");
 need(!msg::roomPolicy("Generated [pk=invalid&ro=1]")&&!msg::roomPolicy("Generated [ro=1]"),"Ambiguous managed policy accepted");need(bool(msg::roomPolicy("Generated [ordinary label]")),"Ordinary bracketed name rejected");
}
void nicknames(){
 QTemporaryDir root;need(root.isValid(),"Generated profile");auto lease=take(storage::ProfileLease::acquire(root.path().toStdString()));auto cfg=take(storage::ConfigFile::createMissing(lease,"biglybt.config","de"));const std::string key="Generated alias room";
 need(bool(cfg.document().replaceValue("azbuddy.chat.shared_nick",b("Initial shared")))&&bool(cfg.document().replaceValue("future-root","i42e")),"Shared nickname fixture");
 const auto legacy="azbuddy.chat.Public: "+key;need(bool(cfg.document().replaceValue(legacy+".shared","i0e"))&&bool(cfg.document().replaceValue(legacy+".nick",b("Legacy alias"))),"Legacy nickname fixture");
 need(bool(cfg.document().replaceValue("azbuddy.dchat.favemap","d"+b("Public:"+key)+"i1ee"))&&bool(cfg.document().replaceValue("azbuddy.dchat.savemsgmap","d"+b("Public:"+key)+"i1ee")),"Legacy favourite/history fixture");need(bool(cfg.save(lease)),"Fixture save");auto profile=take(app::ChatProfile::open(lease));auto catalogue=take(profile->rooms());need(catalogue.size()==1&&catalogue[0].favourite&&catalogue[0].saveHistory,"Legacy-only catalogue missing");
 auto identity=take(profile->identity(key,true));need(identity.settings.nickname=="Legacy alias"&&!identity.settings.sharedNickname,"Legacy nickname/mode not imported");
 need(bool(profile->setSharedNickname(key,true)),"Select shared nickname");need(take(profile->identity(key,false)).settings.nickname=="Initial shared","Changing mode overwrote global nickname");
 need(bool(profile->setSharedNickname(key,false))&&bool(profile->setNickname(key,"New custom")),"Set custom nickname");need(take(profile->identity(key,false)).settings.nickname=="New custom","Legacy value shadows edited custom nickname");
 need(bool(profile->setSharedNickname(key,true))&&bool(profile->setNickname(key,"New shared")),"Set shared nickname");need(take(profile->identity(key,false)).settings.nickname=="New shared","Shared nickname not retained");need(bool(profile->setSharedNickname(key,false)),"Return to custom nickname");need(take(profile->identity(key,false)).settings.nickname=="New custom","Shared edit destroyed room nickname");
 auto options=take(profile->identity(key,false)).settings;options.favourite=false;options.saveHistory=false;need(bool(profile->saveOptions(options)),"Clear legacy booleans");auto current=take(profile->identity(key,false)).settings;need(!current.favourite&&!current.saveHistory,"Legacy map resurrects cleared options on Java handoff");
 cfg=take(storage::ConfigFile::load(lease,"biglybt.config"));need(cfg.document().find("future-root")->integer==42&&cfg.document().find(legacy+".nick")->bytes=="New custom","Unknown/legacy settings lost");
 const auto managed=key+"[pk="+base32(identity.signer.publicKey())+"&ro=1]";auto inherited=take(profile->identity(managed,true));need(inherited.signer.publicKey()==identity.signer.publicKey()&&!inherited.settings.readOnly,"New managed room did not inherit its actual owner's keys");
}
}
int main(int argc,char** argv){QCoreApplication application(argc,argv);try{policy();nicknames();std::cout<<"PASS managed receive policy, live-node hints, per-room/shared/legacy nicknames and key inheritance\n";return 0;}catch(const std::exception& error){std::cerr<<"FAIL "<<error.what()<<'\n';return 1;}}
