// SPDX-License-Identifier: GPL-2.0-or-later
#include "owned_i2p_profile_fixture.hpp"
#include "owned_i2p_service.hpp"
#include "profile_owner.hpp"
#include "magnet_intake.hpp"
#include <libtorrent/session.hpp>
#include <libtorrent/session_params.hpp>
#include <QCoreApplication>
#include <QTcpServer>
#include <atomic>
#include <future>
#include <iostream>
#include <thread>

namespace lt=libtorrent;namespace s=biglybtc::storage;
namespace p=biglybtc::services::i2p::provider;namespace a=biglybtc::app;
using namespace std::chrono_literals;
namespace {
struct Probe {
 std::shared_ptr<const s::ProfileLease> lease;
 std::filesystem::path payload;
 unsigned constructors=0,constructed=0;bool storageUntouched=true,gateReleased=true;
 bool throwBeforeConstruction=false,changeConfiguration=false;
 std::filesystem::path configuration;
 std::vector<std::thread> readers;
 void observe(){
  ++constructors;storageUntouched=storageUntouched&&std::filesystem::is_empty(payload);
  auto ready=std::make_shared<std::promise<bool>>();auto future=ready->get_future();
  readers.emplace_back([held=lease,ready]{auto access=held->sourceAccess();ready->set_value(bool(access));});
  // A failing old control records the blocked reader and returns to the real
  // constructor. Join only after the caller releases its actual outer gate.
  const bool available=future.wait_for(250ms)==std::future_status::ready;
  gateReleased=gateReleased&&available&&(available?future.get():false);
  if(changeConfiguration){
   auto access=lease->sourceAccess(true);if(!access){throw std::runtime_error("Generated config writer gate");}
   auto document=biglybtc::core::Document::parse(owned_i2p_test::read(configuration));
   if(!document||!document->replaceValue("future",owned_i2p_test::b("changed-at-sdk-constructor"))){throw std::runtime_error("Generated config mutation");}
   owned_i2p_test::write(configuration,document->serialize());
  }
  if(throwBeforeConstruction){throw std::runtime_error("Generated SDK-construction failure");}
 }
 void join(){for(auto& reader:readers){if(reader.joinable()){reader.join();}}}
 ~Probe(){join();}
};
thread_local Probe* active=nullptr;
void require(bool value,const char* message){if(!value){throw std::runtime_error(message);}}
}
extern "C" void realSession(lt::session*,lt::session_params&&) asm("__real__ZN10libtorrent7sessionC1EONS_14session_paramsE");
extern "C" void wrappedSession(lt::session*,lt::session_params&&) asm("__wrap__ZN10libtorrent7sessionC1EONS_14session_paramsE");
extern "C" void wrappedSession(lt::session* self,lt::session_params&& params){
 if(active){active->observe();}realSession(self,std::move(params));if(active){++active->constructed;}
}
int main(int argc,char** argv){
 QCoreApplication app(argc,argv);
 try{
  require(argc==2,"Expected bootstrap mode");const std::string_view mode=argv[1];
  const bool service=mode=="service"||mode=="service-pending",pending=mode=="direct-pending"||mode=="service-pending";
  const bool held=mode=="held-gate",stale=mode=="stale-config",ctorFailure=mode=="ctor-failure",changed=mode=="config-during-ctor";
  const bool refused=held||stale||ctorFailure||changed;
  require(service||pending||refused||mode=="direct","Unknown bootstrap mode");
  owned_i2p_test::Fixture fixture;Probe probe;probe.lease=*fixture.lease->retain();probe.payload=fixture.payloadDirectory.path().toStdString();
  probe.configuration=fixture.path/"biglybt.config";probe.throwBeforeConstruction=ctorFailure;probe.changeConfiguration=changed;
  std::optional<biglybtc::engine::PendingMagnet> intent;
  if(pending){
   namespace e=biglybtc::engine;const e::NetworkPolicy policy;
   auto parsed=e::parseMagnetIntent("magnet:?xt=urn:btih:1111111111111111111111111111111111111111&net=I2P",probe.payload,false,policy,e::MagnetNetwork::I2p);require(bool(parsed),"Generated private pending intent");
   auto saved=e::queuePendingMagnet(*fixture.lease,*parsed,policy,e::MagnetNetwork::I2p);require(bool(saved),"Persist generated private metadata intent");intent=std::move(*saved);
  }
  bool prepared=false;std::string failure;
  if(!service){
   auto execute=[&]{
    active=&probe;
    auto owner=pending?p::OwnedProfile::preparePending(*fixture.lease,*intent):p::OwnedProfile::prepare(*fixture.lease,fixture.hash,{},nullptr,stale?std::optional<std::string_view>{"de"}:std::nullopt);
    active=nullptr;prepared=bool(owner);if(!owner){failure=owner.error().message;}else{owner->reset();}
   };
   if(held){auto access=fixture.lease->sourceAccess();require(bool(access),"Generated outer gate");execute();}else{execute();}
  }else{
   QTcpServer port;require(port.listen(QHostAddress::LocalHost,0),"Generated router-port reservation");a::I2pServiceOptions options;options.routerPort=port.serverPort();port.close();
   auto completed=std::make_shared<std::promise<void>>();auto done=completed->get_future();
   std::thread worker([&]{try{
    active=&probe;auto owner=pending?a::OwnedI2pService::startPending(*fixture.lease,*intent,options):a::OwnedI2pService::start(*fixture.lease,fixture.hash,options);active=nullptr;
    prepared=bool(owner);if(!owner){failure=owner.error().message;}else{owner->reset();}
    completed->set_value();
   }catch(...){active=nullptr;completed->set_exception(std::current_exception());}});
   const auto end=std::chrono::steady_clock::now()+25s;
   while(done.wait_for(0ms)!=std::future_status::ready){
    QCoreApplication::processEvents();std::this_thread::sleep_for(1ms);
    if(std::chrono::steady_clock::now()>=end){std::cerr<<"BOOTSTRAP_DEADLINE\n";std::_Exit(2);}
   }
   worker.join();done.get();
  }
  probe.join();
  if(refused){
   require(!prepared,"Invalid bootstrap unexpectedly prepared payload");require(std::filesystem::is_empty(probe.payload),"Failed bootstrap modified payload storage");
   require(probe.constructors==unsigned(ctorFailure||changed),"Failure occurred at the wrong SDK boundary");
   require(probe.constructed==unsigned(changed),"Unexpected actual SDK construction count");
   require(probe.storageUntouched&&probe.gateReleased,"Failure path retained the old SDK/storage ordering");
   std::cout<<"PASS_PRIVATE_SDK_BOOTSTRAP_REFUSAL mode="<<mode<<'\n';return 0;
  }
  require(prepared,failure.c_str());require(probe.constructors==1&&probe.constructed==1,"Expected exactly one actual isolated SDK session");
  std::cout<<"BOOTSTRAP_OBSERVED mode="<<mode<<" constructors="<<probe.constructors<<" storage_untouched="<<probe.storageUntouched<<" source_gate_released="<<probe.gateReleased<<'\n';
  require(probe.storageUntouched,"Payload storage was prepared before actual SDK construction");
  require(probe.gateReleased,"Actual SDK construction retained the caller's filesystem gate");
  std::cout<<"PASS_PRIVATE_SDK_BOOTSTRAP_BOUNDARY\n";
 }catch(std::exception const& error){active=nullptr;std::cerr<<"PRIVATE_BOOTSTRAP "<<error.what()<<'\n';return 1;}
}
