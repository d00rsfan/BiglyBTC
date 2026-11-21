// SPDX-License-Identifier: GPL-2.0-or-later
#include "profile_prepared_tag_attachment.hpp"
#include "sdk_shared_tag_budget.hpp"
#include "tag_preadd_fixture.hpp"
#include <libtorrent/session.hpp>
#include <libtorrent/torrent_info.hpp>
#include <QCoreApplication>
#include <future>
#include <iostream>
namespace lt=libtorrent;using namespace biglybtc;using namespace std::chrono_literals;
namespace {
unsigned checks=0;void check(bool value,const char* why){++checks;if(!value)throw std::runtime_error(why);}
std::string b32(const core::TorrentHash& hash){constexpr char chars[]="ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";std::string out;unsigned bits=0,buffer=0;for(auto v:hash.bytes()){buffer=(buffer<<8)|std::to_integer<unsigned char>(v);bits+=8;while(bits>=5){bits-=5;out+=chars[(buffer>>bits)&31];}}return out;}
struct Fixture{
 shared_transport_fixture::Root root;
 lt::add_torrent_params params=shared_transport_fixture::torrent(root.path/"seed",'q');
 core::TorrentHash hash=core::TorrentHash::parse({params.ti->info_hashes().v1.data(),20}).value();
 std::optional<storage::ProfileLease> lease;
 std::optional<storage::TagManagerStartupMode> mode;
 std::unique_ptr<lt::session> sdk=std::make_unique<lt::session>(shared_transport_fixture::settings());
 std::unique_ptr<engine::ProfileTagRates> rates;
 void write(int rate=16384,bool member=true){std::ofstream out(root.path/"profile/tag.config",std::ios::binary);out<<"d1:3d1:7d1:cd5:rl.upi"<<rate<<"ee1:n13:Generated tag1:ol";if(member)out<<"32:"<<b32(hash);out<<"eeee";}
 explicit Fixture(bool empty=false){std::filesystem::create_directories(root.path/"profile");auto held=storage::ProfileLease::acquire(root.path/"profile");check(bool(held),"Actual generated profile ownership");lease.emplace(std::move(*held));write(empty?0:16384);auto captured=storage::TagManagerStartupMode::capture(*lease);check(bool(captured),"Actual startup mode");mode.emplace(std::move(*captured));auto opened=engine::ProfileTagRates::open(*lease,*mode);check(bool(opened),"Actual common profile tag owner");rates=std::move(*opened);params.flags|=lt::torrent_flags::paused;params.flags&=~lt::torrent_flags::auto_managed;}
 auto prepare(){return rates->prepareAttachment(hash,params.ti->info_hashes(),*sdk);}
};
}
int main(int argc,char** argv){QCoreApplication app(argc,argv);try{
 const std::string mode=argc>1?argv[1]:"bound";Fixture f(mode=="empty");
 if(mode=="held-gate"){
  bool refused=false;{auto gate=f.lease->sourceAccess();check(bool(gate),"Actual outer source gate");refused=!f.prepare();}
  check(refused,"Held source gate reached SDK preparation");auto ready=f.prepare();check(bool(ready),"Released source gate allows actual SDK preparation");
  std::cout<<"PASS_PROFILE_PREADD mode=held-gate assertions="<<checks<<";refused before SDK work,ordinary unlocked retry\n";return 0;
 }
 auto prepared=f.prepare();check(bool(prepared)&&(*prepared)->current(),"Pending source/cohort target");
 auto duplicate=f.prepare();check(!duplicate&&(*prepared)->current(),"Duplicate pending reservation refuses without poisoning original");
 if(mode=="abandoned"||mode=="abandoned-installed"){
  auto fresh=f.params;auto old=f.params;
  if(mode=="abandoned-installed"){check(bool((*prepared)->install(old)),"Arm metadata before dropping pending bundle");}
  prepared->reset();auto retried=f.prepare();check(bool(retried),"Abandoned pending reservation blocks retry");
  check(bool((*retried)->install(fresh)),"Fresh retry installs exact paused target");auto target=f.sdk->add_torrent(fresh);check((*retried)->boundTo(target),"Actual retried constructor target");
  if(mode=="abandoned-installed"){lt::error_code ec;auto duplicate=f.sdk->add_torrent(old,ec);(void)duplicate;check(bool(ec)&&(*retried)->boundTo(target),"Late old parameter copy cannot poison the retried binding");}
 }else if(mode=="worker"){
  auto wrong=std::async(std::launch::async,[&]{return f.prepare();});check(!wrong.get()&&(*prepared)->current(),"Source-reading preparation retains controller worker affinity");
 }else if(mode=="no-lease"){
  check(bool(f.rates->close())&&!(*prepared)->current(),"Profile close revokes retained bundle");f.mode.reset();f.lease.reset();auto reopened=storage::ProfileLease::acquire(f.root.path/"profile");check(bool(reopened),"Pending bundle retains no profile lease");
 }else if(mode=="sdk-abort"){
  f.sdk.reset();check(!(*prepared)->current(),"SDK abort revokes pending target");
 }else if(mode=="bound"||mode=="empty"){
  auto unsafe=f.params;unsafe.flags&=~lt::torrent_flags::paused;check(!(*prepared)->install(unsafe)&&unsafe.extensions.empty(),"Running parameters refuse without mutation");
  lt::torrent_handle target;bool installed=false;std::exception_ptr error;std::promise<void> done;auto completed=done.get_future();std::thread worker;bool whileGateHeld=false;
  {
   auto gate=f.lease->sourceAccess();check(bool(gate),"Actual controller source gate");
   worker=std::thread([&]{try{installed=bool((*prepared)->install(f.params));if(installed)target=f.sdk->add_torrent(f.params);}catch(...){error=std::current_exception();}done.set_value();});
   whileGateHeld=completed.wait_for(4s)==std::future_status::ready;
  }
  worker.join();if(error)std::rethrow_exception(error);check(whileGateHeld&&installed,"Cross-worker install and actual SDK constructor need no source gate");
  check((*prepared)->boundTo(target),"Complete constructor bundle binds exact actual handle");check(!(*prepared)->install(f.params),"Bundle is one-shot");
  check(bool(f.rates->synchronize(*f.sdk))&&(*prepared)->boundTo(target),"Completed pending target harvested without no-op revocation");
  f.write(8192,false);check(bool(f.rates->synchronize(*f.sdk))&&!(*prepared)->current(),"Source changes reach actual harvested binding and revoke old bundle");
 }else{
  check(bool((*prepared)->install(f.params)),"Install paused pending parameters");
  if(mode=="stale"){
   f.write(8192);check(bool(f.rates->synchronize(*f.sdk))&&!(*prepared)->current(),"Source update cancels unconstructed callback");
  }else if(mode=="close"){
   check(bool(f.rates->close())&&!(*prepared)->current(),"Controller close cancels unconstructed callback");
  }else if(mode=="damaged"){
   {std::ofstream out(f.root.path/"profile/tag.config.saving");out<<"generated interrupted save";}
   check(!f.rates->synchronize(*f.sdk)&&f.sdk->is_paused()&&!(*prepared)->current(),"Damaged source holds whole SDK cohort and revokes pending target");
   std::filesystem::remove(f.root.path/"profile/tag.config.saving");check(bool(f.rates->synchronize(*f.sdk))&&!f.sdk->is_paused()&&!(*prepared)->current(),"Recovery preserves cancellation of old callback");
  }else if(mode=="foreign"){
   lt::session foreign(shared_transport_fixture::settings());auto outsider=foreign.add_torrent(f.params);check(outsider.is_valid()&&!(*prepared)->boundTo(outsider)&&!(*prepared)->current(),"Actual foreign SDK callback refuses entire pending bundle");
  }else if(mode=="cohort"){
   lt::session sibling(shared_transport_fixture::settings());auto joined=f.rates->synchronize(sibling);check(bool(joined)==engine::SdkSharedQuotaScope::supported(),"Actual SDK cohort admission follows shared runtime support");
   check(joined?!(*prepared)->current():(*prepared)->current(),"Only successful cohort admission revokes pending source witness");
  }else{throw std::runtime_error("Unknown fixture mode");}
  if(mode!="foreign"&&mode!="cohort"){auto target=f.sdk->add_torrent(f.params);check(target.is_valid()&&!(*prepared)->boundTo(target),"Refused constructor remains unattached and paused");}
 }
 std::cout<<"PASS_PROFILE_PREADD mode="<<mode<<" assertions="<<checks<<";actual source/SDK,pending reservation,serialized harvest/cancellation,no lease or storage grant\n";
}catch(std::exception const& error){std::cerr<<"PROFILE_PREADD "<<error.what()<<'\n';return 1;}}
