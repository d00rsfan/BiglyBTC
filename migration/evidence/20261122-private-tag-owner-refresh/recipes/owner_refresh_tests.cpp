// SPDX-License-Identifier: GPL-2.0-or-later
#include "owned_i2p_profile_fixture.hpp"
#include "owned_i2p_service.hpp"
#include "profile_tag_rates.hpp"
#include <QCoreApplication>
#include <QTcpServer>
#include <QTcpSocket>
#include <QHostAddress>
#include <libtorrent/session.hpp>
#include <future>
#include <iostream>
#include <thread>
namespace core=biglybtc::core;namespace e=biglybtc::engine;namespace s=biglybtc::storage;namespace a=biglybtc::app;namespace lt=libtorrent;
namespace {
void check(bool value,const char* why){if(!value)throw std::runtime_error(why);}
void tag(owned_i2p_test::Fixture& f){using owned_i2p_test::dict;using owned_i2p_test::b;std::string id;unsigned value=0,bits=0;constexpr char alphabet[]="ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";for(auto byte:f.hash.bytes()){value=(value<<8)|std::to_integer<unsigned char>(byte);bits+=8;while(bits>=5){bits-=5;id+=alphabet[(value>>bits)&31];}}owned_i2p_test::write(f.path/"tag.config",dict({{"3",dict({{"7",dict({{"c",dict({{"rl.down","i16384e"}})},{"n",b("Generated service tag")},{"o","l"+b(id)+"e"}})}})}}));}
lt::settings_pack local(){lt::settings_pack q;q.set_str(lt::settings_pack::listen_interfaces,"127.0.0.1:0");q.set_str(lt::settings_pack::dht_bootstrap_nodes,"");for(int flag:{lt::settings_pack::enable_dht,lt::settings_pack::enable_lsd,lt::settings_pack::enable_upnp,lt::settings_pack::enable_natpmp})q.set_bool(flag,false);return q;}
}
int main(int argc,char** argv){QCoreApplication app(argc,argv);QTcpServer server;check(server.listen(QHostAddress::LocalHost,0),"Owned loopback I2CP holding server");
 QObject::connect(&server,&QTcpServer::newConnection,&server,[&]{while(server.hasPendingConnections()){auto* socket=server.nextPendingConnection();socket->setReadBufferSize(4096);}});
 const auto port=server.serverPort();const std::string mode=argc>1?argv[1]:"changed";std::string failure;
 std::thread worker([&]{try{
  owned_i2p_test::Fixture f,foreign;tag(f);tag(foreign);auto startup=s::TagManagerStartupMode::capture(*f.lease),otherMode=s::TagManagerStartupMode::capture(*foreign.lease);check(bool(startup)&&bool(otherMode),"Actual immutable startup modes");
  auto rates=e::ProfileTagRates::open(*f.lease,*startup);check(bool(rates),"Actual common controller");lt::session normal(local());check(bool((*rates)->prepare(f.hash,normal)),"Actual ordinary SDK cohort participant");
  a::I2pServiceOptions options;options.routerPort=port;auto service=a::OwnedI2pService::startWithTagRates(*f.lease,f.hash,**rates,options,{},&*startup);check(bool(service),service?"":service.error().message.c_str());
  const auto state=owned_i2p_test::read(f.state()),inventory=owned_i2p_test::read(f.path/"downloads.config"),oldTags=owned_i2p_test::read(f.path/"tag.config");const auto payload=std::filesystem::path(f.payloadDirectory.path().toStdString())/"owned.bin";struct stat before{};check(::stat(payload.c_str(),&before)==0,"Actual prepared payload identity");
  auto store=s::ManualTagStore::open(*f.lease);check(bool(store),"Actual source/CAS store");auto snapshot=(*store)->snapshot();check(bool(snapshot),"Actual current preimage");
  if(mode=="refused"){check(bool((*store)->reload()),"Actual stale-store generation setup");}
  auto wrong=e::ProfileTagRates::open(mode=="same-profile"?*f.lease:*foreign.lease,mode=="same-profile"?*startup:*otherMode);check(bool(wrong),"Actual independent controller");
  unsigned calls=0;auto operation=[&]()->std::expected<void,s::Error>{
   ++calls;
   if(mode=="metadata"){owned_i2p_test::write(f.state(),state.substr(0,state.size()-1)+owned_i2p_test::b("x-refresh")+owned_i2p_test::b("generated")+"e");return {};}
   if(mode=="key"){auto bytes=owned_i2p_test::read(f.key);bytes[0]^=1;owned_i2p_test::write(f.key,bytes);return {};}
   if(mode=="saving"){owned_i2p_test::write(f.path/"tag.config.saving","generated interrupted save");return {};}
   auto saved=(*store)->apply(*snapshot,core::SavedTagSetRateLimit{7,false,mode=="noop"?16384:8192});if(!saved){return std::unexpected(saved.error());}return {};
  };
  std::expected<void,s::Error> result;
  if(mode=="held"){auto gate=f.lease->sourceAccess();check(bool(gate),"Actual outer source gate");result=(*service)->updateTagSource(**rates,operation);}
  else{result=(*service)->updateTagSource(mode=="foreign"||mode=="same-profile"?**wrong:**rates,operation);}
  const bool refused=mode=="foreign"||mode=="same-profile"||mode=="held"||mode=="refused";
  const bool damaged=mode=="metadata"||mode=="key"||mode=="saving";
  check(bool(result)==!(refused||damaged),result?"Unexpected source acceptance":result.error().message.c_str());
  check(calls==unsigned(!(mode=="foreign"||mode=="same-profile"||mode=="held")),"Refusal must precede actual writer callback");
  check((*service)->sourceCurrent()!=damaged,"Exact source remains current only after a complete safe refresh");
  const auto savedTags=owned_i2p_test::read(f.path/"tag.config");check((mode=="changed")?(savedTags!=oldTags):(savedTags==oldTags),"Actual tag source write/no-op/refusal behavior");
  if(mode=="changed"){auto latest=(*store)->reload();check(bool(latest)&&latest->tags(),"Actual durable tag reload");const auto* type=latest->tags()->source->find("3");const auto* record=type?type->find("7"):nullptr;check(record!=nullptr,"Actual saved tag record");auto limits=core::readSavedTagRateLimits(*record);check(bool(limits)&&limits->download==8192,"Actual saved limit changed");}
  struct stat after{};check(::stat(payload.c_str(),&after)==0&&before.st_ino==after.st_ino&&before.st_dev==after.st_dev&&before.st_size==after.st_size,"Tag refresh preserves actual payload identity and size");
  check(inventory==owned_i2p_test::read(f.path/"downloads.config"),"Tag refresh preserves original inventory");if(mode!="metadata"){check(state==owned_i2p_test::read(f.state()),"Tag refresh preserves prepared resume state");}
  check(bool((*service)->closeWithoutCheckpoint())&&(*service)->joined(),"Actual private Qt/SDK/source workers joined");service->reset();
  std::cout<<"PASS_PRIVATE_OWNER_REFRESH mode="<<mode<<";actual starting private service/shared SDK cohort,source/CAS callback,pending token renewal,source workers,exact unrelated metadata and payload preservation;no router/payload credit\n";
 }catch(std::exception const& error){failure=error.what();}QMetaObject::invokeMethod(&app,&QCoreApplication::quit,Qt::QueuedConnection);});
 app.exec();worker.join();if(!failure.empty()){std::cerr<<"PRIVATE_OWNER_REFRESH "<<failure<<'\n';return 1;}return 0;
}
