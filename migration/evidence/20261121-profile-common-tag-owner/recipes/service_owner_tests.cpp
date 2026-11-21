// SPDX-License-Identifier: GPL-2.0-or-later
#include "owned_i2p_profile_fixture.hpp"
#include "owned_i2p_service.hpp"
#include "profile_tag_rates.hpp"
#include <QCoreApplication>
#include <future>
#include <iostream>
namespace e=biglybtc::engine;namespace s=biglybtc::storage;namespace a=biglybtc::app;
namespace {
void check(bool value,const char* why){if(!value)throw std::runtime_error(why);}
void tag(owned_i2p_test::Fixture& f){using owned_i2p_test::dict;using owned_i2p_test::b;std::string id;unsigned value=0,bits=0;constexpr char alphabet[]="ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";for(auto byte:f.hash.bytes()){value=(value<<8)|std::to_integer<unsigned char>(byte);bits+=8;while(bits>=5){bits-=5;id+=alphabet[(value>>bits)&31];}}owned_i2p_test::write(f.path/"tag.config",dict({{"3",dict({{"7",dict({{"c",dict({{"rl.down","i16384e"}})},{"n",b("Generated service tag")},{"o","l"+b(id)+"e"}})}})}}));}
}
int main(int argc,char** argv){QCoreApplication app(argc,argv);try{
 const std::string mode=argc>1?argv[1]:"tagged";
 auto run=std::async(std::launch::async,[&]{
  owned_i2p_test::Fixture f,foreign;tag(f);tag(foreign);
  auto startup=s::TagManagerStartupMode::capture(*f.lease),other=s::TagManagerStartupMode::capture(*foreign.lease);check(bool(startup)&&bool(other),"Actual service startup");
  const bool wrong=mode=="foreign";auto root=e::ProfileTagRates::open(wrong?*foreign.lease:*f.lease,wrong?*other:*startup);check(bool(root),"Actual common controller");
  const auto state=owned_i2p_test::read(f.state()),inventory=owned_i2p_test::read(f.path/"downloads.config"),source=owned_i2p_test::read(f.path/"tag.config");
  a::I2pServiceOptions options;options.routerPort=1;
  auto service=mode=="default"?a::OwnedI2pService::start(*f.lease,f.hash,options,{},&*startup):a::OwnedI2pService::startWithTagRates(*f.lease,f.hash,**root,options,{},&*startup);
  if(mode=="tagged"){
   check(bool(service),service?"":service.error().message.c_str());check(!std::filesystem::is_empty(f.payloadDirectory.path().toStdString()),"Actual service storage");
   auto duplicate=(*service)->addWithTagRates(f.hash,**root);check(!duplicate,"Duplicate live member refuses");
   auto closed=(*service)->closeWithoutCheckpoint();check(bool(closed)&&(*service)->joined(),"Actual private Qt/SDK/source workers joined");service->reset();
   check((*root)->matchesProfile(*f.lease,*startup),"Service destruction cannot destroy common root");check(bool((*root)->close()),"External common root close");
  }else{
   check(!service,"Default/foreign private source must refuse");check(std::filesystem::is_empty(f.payloadDirectory.path().toStdString()),"Refusal before storage");
  }
  check(state==owned_i2p_test::read(f.state())&&inventory==owned_i2p_test::read(f.path/"downloads.config")&&source==owned_i2p_test::read(f.path/"tag.config"),"Preparation/terminal close preserves original state and tag source");
 });run.get();std::cout<<"PASS_COMMON_TAG_SERVICE mode="<<mode<<";actual service preparation on IO worker,explicit common controller,default/foreign pre-storage refusal,real Qt/SDK terminal joins;no router/payload credit\n";
}catch(std::exception const& error){std::cerr<<"COMMON_TAG_SERVICE "<<error.what()<<'\n';return 1;}}
