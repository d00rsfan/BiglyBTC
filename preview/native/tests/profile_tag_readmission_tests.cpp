// SPDX-License-Identifier: GPL-2.0-or-later
#include "profile_tag_rates.hpp"
#include "sdk_shared_tag_budget.hpp"
#include "persistent_session.hpp"
#include "owned_engine_fixture.hpp"
#include <QCoreApplication>
#include <iostream>
#include <thread>
using namespace biglybtc;using namespace std::chrono_literals;
void require(bool value,const char* why){if(!value)throw std::runtime_error(why);}
int main(int argc,char** argv){QCoreApplication app(argc,argv);try{
 if(!engine::SdkSharedQuotaScope::supported()){return 77;}
 test::OwnedEngineFixture fixture(false);auto held=storage::ProfileLease::acquire(fixture.native());require(bool(held),"Actual profile lease");auto mode=storage::TagManagerStartupMode::capture(*held);require(bool(mode),"Actual startup");
 auto made=engine::ProfileTagRates::open(*held,*mode);require(bool(made),"Actual common profile controller");std::shared_ptr<engine::ProfileTagRates> root(std::move(*made));
 libtorrent::session sibling(test::localSettings());require(bool(root->prepare(*fixture.hash,sibling)),"Actual sibling SDK admitted before normal torrent");
 auto owner=engine::PersistentSession::create({701,702,703});require(bool(owner),"Actual qualified normal owner");require(bool((*owner)->configureTagManagerStartup(*mode))&&bool((*owner)->configureCommonTagRateOwner(*held,root)),"Inject actual common controller");require(bool((*owner)->startIdle(*held)),"Normal SDK startup");
 for(unsigned round=0;round<2;++round){
  auto admitted=(*owner)->startManaged(*held,{*fixture.hash,0,true,false,true});require(bool(admitted),admitted?"":admitted.error().message.c_str());require(!(*owner)->tagRateFailure(),"Actual normal attachment with two SDK owners");
  require(bool((*owner)->stopManaged(*fixture.hash)),"Actual retained normal stop");const auto end=std::chrono::steady_clock::now()+15s;bool retired=false;
  while(std::chrono::steady_clock::now()<end){require(bool((*owner)->poll()),"Normal removal alert pump");const auto rows=(*owner)->managedDownloads();retired=std::ranges::none_of(rows,[](const auto& row){return row.retained;});if(retired){break;}QCoreApplication::processEvents();std::this_thread::sleep_for(10ms);}
  require(retired,"Actual normal SDK removal/join fence before readmission");
 }
 const auto closed=(*owner)->closeWithoutCheckpoint(std::chrono::steady_clock::now()+10s);require(closed.sessionJoined,"Actual normal SDK joined");require(bool(root->release(sibling))&&bool(root->close()),"Surviving sibling and profile scope retire independently");
 std::cout<<"PASS_PROFILE_TAG_READMISSION two actual SDKs;normal constructor/stop/removal/readmission and independent final joins\n";
 }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
