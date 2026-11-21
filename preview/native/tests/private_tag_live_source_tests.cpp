// SPDX-License-Identifier: GPL-2.0-or-later
#include "profile_prepared_tag_attachment.hpp"
#include "private_tag_rate_source.hpp"
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
 const std::string mode=argc>1?argv[1]:"current";Fixture f(mode=="empty");
 std::unique_ptr<lt::session> sibling;
 if(mode=="sibling-abort"){sibling=std::make_unique<lt::session>(shared_transport_fixture::settings());check(bool(f.rates->prepare(f.hash,*sibling)),"Admitted real sibling SDK");}
 auto pending=f.prepare();check(bool(pending),"Actual pre-add target");
 if(!engine::SdkSharedQuotaScope::supported()){
  check(!f.rates->refreshPrivateSource(f.hash,*f.sdk,{})&&(*pending)->current(),"ABI1 refuses live private source without poisoning ordinary target");
  std::cout<<"PASS_PRIVATE_TAG_LIVE unsupported actual ABI1 clean refusal\n";return 0;
 }
 auto old=f.rates->preparePrivateSource(f.hash,*f.sdk,**pending);check(bool(old),"Actual initial private source");
 check(!f.rates->refreshPrivateSource(f.hash,*f.sdk,{}),"Unconstructed target cannot receive live readiness");check(old->current()&&(*pending)->current(),"Unbound refusal cannot revoke pre-add source");
 check(bool((*pending)->install(f.params)),"Exact actual paused add parameters");auto handle=f.sdk->add_torrent(f.params);check((*pending)->boundTo(handle),"Actual SDK constructor attached complete classes");
 auto ready=f.rates->refreshPrivateSource(f.hash,*f.sdk,handle);check(bool(ready)&&ready->current(),"Actual current bound source capture harvests constructor handle");
 check(f.rates->ownsPrivateSource(*ready),"Actual stable controller owns its private source");
 {auto other=engine::ProfileTagRates::open(*f.lease,*f.mode);check(bool(other)&&!(*other)->ownsPrivateSource(*ready),"A different controller for the same profile cannot consume private source ownership");}
 engine::PrivateTagRateSource absent;check(!f.rates->ownsPrivateSource(absent),"Absent private source has no controller owner");
 auto otherWorker=std::async(std::launch::async,[&]{return f.rates->ownsPrivateSource(*ready);});check(!otherWorker.get(),"Stable owner check retains creating-worker affinity");
 auto store=storage::ManualTagStore::open(*f.lease);check(bool(store),"Independent actual source reader");auto snapshot=(*store)->snapshot();check(bool(snapshot),"Fresh source snapshot");
 const auto matches=[&]{return ready->matches(*f.lease,f.hash,*snapshot,f.params.ti->info_hashes(),*f.sdk);};check(matches(),"Exact actual SDK/full identity and current source");
 if(mode=="rate"||mode=="disabled"||mode=="removed-tag"||mode=="empty"){
  f.write(mode=="disabled"?-1:8192,mode!="removed-tag");snapshot=(*store)->reload();check(bool(snapshot)&&!matches(),"Old private proof cannot match changed source before effects");
  auto next=f.rates->refreshPrivateSource(f.hash,*f.sdk,handle);check(bool(next)&&next->current()&&!ready->current()&&!old->current(),"Changed source applies and renews without stale pre-add epoch");
  check(f.rates->ownsPrivateSource(*old)&&f.rates->ownsPrivateSource(*next),"Stable actual controller ownership spans source epoch changes");
  ready=std::move(next);check(matches()&&!(*pending)->boundTo(handle),"Live source survives expired original constructor tokens");
 }else if(mode=="cohort"){
  lt::session other(shared_transport_fixture::settings());check(bool(f.rates->synchronize(other))&&!ready->current(),"Real admitted SDK changes complete cohort");
  ready=f.rates->refreshPrivateSource(f.hash,*f.sdk,handle);check(bool(ready)&&matches(),"Bound target obtains complete new SDK cohort without re-add");
 }else if(mode=="sibling-abort"){
  sibling.reset();check(!ready->current(),"Abort revokes complete cohort passively");ready=f.rates->refreshPrivateSource(f.hash,*f.sdk,handle);check(bool(ready)&&matches(),"Actual dead SDK retirement permits fresh surviving target proof");
 }else if(mode=="foreign"){
  lt::session other(shared_transport_fixture::settings());check(!f.rates->refreshPrivateSource(f.hash,other,handle)&&matches(),"Unadmitted foreign SDK cannot poison current cohort");
  const auto wrong=core::TorrentHash::parse(std::string(20,'x')).value();check(!f.rates->refreshPrivateSource(wrong,*f.sdk,handle)&&matches(),"Foreign canonical proposal cannot change source");
  check(!f.rates->refreshPrivateSource(f.hash,*f.sdk,{})&&matches(),"Foreign handle refuses before effects");
  auto full=f.params.ti->info_hashes();lt::sha256_hash extra;extra[0]=51;full.v2=extra;check(!ready->matches(*f.lease,f.hash,*snapshot,full,*f.sdk)&&matches(),"Full hybrid identity remains exact");
 }else if(mode=="remove"){
  f.sdk->remove_torrent(handle);auto until=std::chrono::steady_clock::now()+5s;while(handle.is_valid()&&std::chrono::steady_clock::now()<until)std::this_thread::sleep_for(10ms);
  check(!handle.is_valid()&&!ready->current()&&!f.rates->refreshPrivateSource(f.hash,*f.sdk,handle),"Actual removed handle invalidates and cannot refresh");
 }else if(mode=="gate"){
  std::future<bool> observed;bool fast=false,refused=false;
  {auto gate=f.lease->sourceAccess();check(bool(gate),"Actual outer filesystem gate");refused=!f.rates->refreshPrivateSource(f.hash,*f.sdk,handle);observed=std::async(std::launch::async,[&]{return matches();});fast=observed.wait_for(3s)==std::future_status::ready;}
  check(refused&&fast&&observed.get()&&matches(),"Live capture refuses held gate while passive proof has no filesystem/SDK wait");
  auto foreign=std::async(std::launch::async,[&]{return f.rates->refreshPrivateSource(f.hash,*f.sdk,handle);});check(!foreign.get()&&matches(),"Foreign control worker refuses without source changes");
 }else if(mode=="saving"){
  {std::ofstream out(f.root.path/"profile/tag.config.saving");out<<"generated interrupted save";}
  check(!f.rates->refreshPrivateSource(f.hash,*f.sdk,handle)&&!ready->current()&&f.sdk->is_paused(),"Unsettled source holds SDK and revokes proof");
  std::filesystem::remove(f.root.path/"profile/tag.config.saving");ready=f.rates->refreshPrivateSource(f.hash,*f.sdk,handle);check(bool(ready)&&matches(),"Recovery captures fresh bound source without re-add");
 }else if(mode=="sdk-abort"){
  f.sdk.reset();check(!ready->current(),"Actual target SDK destruction revokes the bound proof without another control call");
 }else if(mode=="no-lease"){
  check(bool(f.rates->close())&&!ready->current(),"Explicit current root close");check(bool((*store)->close()),"Close actual independent source store");f.mode.reset();f.lease.reset();
  auto reacquired=storage::ProfileLease::acquire(f.root.path/"profile");check(bool(reacquired),"Live target proof cannot retain the profile lease after owners close");
 }else if(mode=="close"){
  check(bool(f.rates->close())&&!ready->current()&&!f.rates->refreshPrivateSource(f.hash,*f.sdk,handle),"Root close revokes current bound proof");
 }else if(mode!="current"){throw std::runtime_error("Unknown bound source mode");}
 std::cout<<"PASS_PRIVATE_TAG_LIVE mode="<<mode<<" assertions="<<checks<<";actual bound census,current SDK effects,complete cohort and exact source;no private source rebase/storage/transport grant\n";
}catch(std::exception const& error){std::cerr<<"PRIVATE_TAG_LIVE "<<error.what()<<'\n';return 1;}}
