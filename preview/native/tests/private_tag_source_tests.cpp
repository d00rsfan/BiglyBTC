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
 const std::string mode=argc>1?argv[1]:"match";Fixture f;
 std::unique_ptr<lt::session> sibling;
 if(mode=="sibling-abort"){sibling=std::make_unique<lt::session>(shared_transport_fixture::settings());check(bool(f.rates->prepare(f.hash,*sibling)),"Admit sibling before pending target");}
 auto pending=f.prepare();check(bool(pending)&&(*pending)->current(),"Actual pending SDK target");
 if(!engine::SdkSharedQuotaScope::supported()){
  check(!f.rates->preparePrivateSource(f.hash,*f.sdk,**pending)&&(*pending)->current(),"Legacy SDK refuses private source readiness without poisoning current target");
  std::cout<<"PASS_PRIVATE_TAG_SOURCE unsupported actual ABI1 clean refusal;no storage or SDK role claim\n";return 0;
 }
 auto store=storage::ManualTagStore::open(*f.lease);check(bool(store),"Actual fresh source store");auto snapshot=(*store)->snapshot();check(bool(snapshot),"Actual fresh snapshot");
 auto ready=f.rates->preparePrivateSource(f.hash,*f.sdk,**pending);check(bool(ready)&&ready->current(),"Actual sealed shared SDK/source preparation");
 const auto matches=[&]{return ready->matches(*f.lease,f.hash,*snapshot,f.params.ti->info_hashes(),*f.sdk);};check(matches(),"Current source and exact target/SDK match");
 if(mode=="checking"||mode=="checking-stale"){
  auto params=f.params;params.flags|=lt::torrent_flags::stop_when_ready;params.flags&=~lt::torrent_flags::paused;
  check(!(*pending)->install(params)&&params.extensions.empty(),"Ordinary paused API cannot install a running check");
  auto unsafe=params;unsafe.flags&=~lt::torrent_flags::stop_when_ready;check(!(*pending)->installForChecking(unsafe)&&unsafe.extensions.empty(),"Checking requires native stop_when_ready");
  unsafe=params;unsafe.ti.reset();unsafe.info_hashes=params.ti->info_hashes();check(!(*pending)->installForChecking(unsafe)&&unsafe.extensions.empty(),"Checking cannot admit a metadata-less magnet");
  unsafe=params;unsafe.flags|=lt::torrent_flags::auto_managed;check(!(*pending)->installForChecking(unsafe)&&unsafe.extensions.empty(),"Checking cannot auto-resume");
  check(bool((*pending)->installForChecking(params)),"Explicit full metadata checking capability");
  if(mode=="checking-stale"){f.write(8192);check(bool(f.rates->synchronize(*f.sdk))&&!ready->current(),"Source changes before actual checking constructor");}
  auto handle=f.sdk->add_torrent(params);check(handle.is_valid(),"Actual SDK checking add");
  check((*pending)->boundTo(handle)==(mode=="checking"),"Checking constructor binds only current source/classes");
  const auto end=std::chrono::steady_clock::now()+8s;bool stopped=false;
  do{auto status=handle.status();stopped=bool(status.flags&lt::torrent_flags::paused)&&!(status.flags&lt::torrent_flags::stop_when_ready)
      &&(status.state==lt::torrent_status::seeding||status.state==lt::torrent_status::finished||status.state==lt::torrent_status::downloading);
    check(status.total_payload_download==0&&status.total_payload_upload==0,"Checking add transferred payload before queue authorization");
    if(stopped){break;}
    std::this_thread::sleep_for(10ms);
  }while(std::chrono::steady_clock::now()<end);
  check(stopped,"Native ready-state pause must happen without a post-add resume");
 }else if(mode=="match"){
  engine::PrivateTagRateSource absent;check(!absent.current()&&!absent.matches(*f.lease,f.hash,*snapshot,f.params.ti->info_hashes(),*f.sdk),"Absent witness refuses");
  const std::array ids{7};auto generic=f.rates->prepareSource(*f.lease,**store,*snapshot,ids,*f.sdk);check(bool(generic)&&generic->matches(*f.lease,**store,*snapshot,7),"Generic actor proof still binds its store");
  auto other=storage::ManualTagStore::open(*f.lease);check(bool(other),"Second current source reader");auto otherSnapshot=(*other)->snapshot();check(bool(otherSnapshot),"Second current capture");
  check(ready->matches(*f.lease,f.hash,*otherSnapshot,f.params.ti->info_hashes(),*f.sdk),"Sealed private preparation matches an independent fresh reader");
  check(!generic->matches(*f.lease,**other,*otherSnapshot,7),"Generic writer/store lineage remains exact");
 }else if(mode=="foreign"){
  Fixture other;check(!ready->matches(*other.lease,other.hash,*snapshot,f.params.ti->info_hashes(),*f.sdk),"Foreign ownership refuses even matching metadata");
  lt::session foreign(shared_transport_fixture::settings());check(!ready->matches(*f.lease,f.hash,*snapshot,f.params.ti->info_hashes(),foreign),"Actual foreign SDK refuses");
  check(!f.rates->preparePrivateSource(f.hash,foreign,**pending)&&matches(),"Foreign SDK proposal does not admit a cohort or revoke current witness");
  auto wrong=core::TorrentHash::parse(std::string(20,'x')).value();check(!f.rates->preparePrivateSource(wrong,*f.sdk,**pending)&&matches(),"Wrong canonical target proposal does not poison readiness");
  auto full=f.params.ti->info_hashes();lt::sha256_hash extra;extra[0]=51;full.v2=extra;check(!ready->matches(*f.lease,f.hash,*snapshot,full,*f.sdk)&&matches(),"Changed hybrid side cannot substitute full SDK identity");
 }else if(mode=="drop"){
  pending->reset();check(!ready->current()&&!matches(),"Dropped unbound target passively revokes sealed source preparation");auto retry=f.prepare();check(bool(retry),"Cancelled private-source target can retry");
 }else if(mode=="bound"){
  check(bool((*pending)->install(f.params)),"Actual paused constructor parameters");auto handle=f.sdk->add_torrent(f.params);check((*pending)->boundTo(handle)&&matches(),"Actual constructor binding retains prepared source");
  check(bool(f.rates->synchronize(*f.sdk))&&matches(),"Census harvest does not invalidate unchanged sealed source");f.sdk->remove_torrent(handle);
  auto end=std::chrono::steady_clock::now()+5s;while(handle.is_valid()&&std::chrono::steady_clock::now()<end)std::this_thread::sleep_for(10ms);
  check(!handle.is_valid()&&!ready->current(),"Actual removed target revokes source preparation");
 }else if(mode=="source"){
  f.write(8192);snapshot=(*store)->reload();check(bool(snapshot)&&!matches(),"Fresh source preimage mismatch refuses before controller refresh");
  check(bool(f.rates->synchronize(*f.sdk))&&!ready->current(),"Source effects passively revoke old preparation");
 }else if(mode=="saving"){
  {std::ofstream out(f.root.path/"profile/tag.config.saving");out<<"generated interrupted save";}
  check(!f.rates->preparePrivateSource(f.hash,*f.sdk,**pending)&&!ready->current()&&f.sdk->is_paused(),"Unsettled source holds real session and refuses source preparation");
  std::filesystem::remove(f.root.path/"profile/tag.config.saving");check(bool(f.rates->synchronize(*f.sdk))&&!ready->current(),"Recovery cannot revive old sealed preparation");
 }else if(mode=="cohort"){
  lt::session joined(shared_transport_fixture::settings());check(bool(f.rates->synchronize(joined))&&!ready->current(),"Admitted actual SDK changes complete cohort witness");
 }else if(mode=="sibling-abort"){
  sibling.reset();check(!ready->current()&&!matches()&&(*pending)->current(),"Other SDK abort revokes complete cohort even while target SDK remains current");
 }else if(mode=="gate"){
  bool refused=false,passive=false;std::future<bool> observed;
  {auto gate=f.lease->sourceAccess();check(bool(gate),"Actual same-OFD source gate");refused=!f.rates->preparePrivateSource(f.hash,*f.sdk,**pending);observed=std::async(std::launch::async,[&]{return matches();});passive=observed.wait_for(4s)==std::future_status::ready;}
  check(refused&&passive&&observed.get()&&matches(),"Preparation refuses held gate;passive match works cross-thread with no filesystem/SDK wait");
  auto wrong=std::async(std::launch::async,[&]{return f.rates->preparePrivateSource(f.hash,*f.sdk,**pending);});check(!wrong.get()&&matches(),"Creating-worker preparation affinity remains exact");
 }else if(mode=="close"){
  check(bool(f.rates->close())&&!ready->current(),"Root controller close revokes sealed source");check(bool((*store)->close()),"Release actual test store");f.mode.reset();f.lease.reset();auto reacquired=storage::ProfileLease::acquire(f.root.path/"profile");check(bool(reacquired),"Sealed source and pending target retain no profile lease");
 }else{throw std::runtime_error("Unknown private source fixture mode");}
 std::cout<<"PASS_PRIVATE_TAG_SOURCE mode="<<mode<<" assertions="<<checks<<";actual SDK/source/cohort preparation,exact full target,no generic CAS or private storage permission yet\n";
}catch(std::exception const& error){std::cerr<<"PRIVATE_TAG_SOURCE "<<error.what()<<'\n';return 1;}}
