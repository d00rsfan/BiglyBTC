// SPDX-License-Identifier: GPL-2.0-or-later
#include "download_history_store.hpp"
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <stdexcept>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
namespace s=biglybtc::storage;namespace c=biglybtc::core;namespace fs=std::filesystem;
namespace {
constexpr auto Active=s::DownloadHistorySide::Active,Dead=s::DownloadHistorySide::Dead;
constexpr const char* Journal="BiglyBTC.download-history.config";
void check(bool ok,const char* message){if(!ok){throw std::runtime_error(message);}}
template<class T>T take(std::expected<T,s::Error> value){if(!value){throw std::runtime_error(value.error().message);}return std::move(*value);}
void take(std::expected<void,s::Error> value){if(!value){throw std::runtime_error(value.error().message);}}
template<class T>void error(const std::expected<T,s::Error>& value,s::ErrorCode code){check(!value&&value.error().code==code,"Wrong history store refusal");}
std::string b(std::string_view value){return std::to_string(value.size())+':'+std::string(value);}
std::string row(int id){return "d1:ui"+std::to_string(id)+"e1:n3:old1:s5:/path1:ai1000e1:ci-1e1:ri0e6:future"+b(std::string("x\0y",3))+"e";}
std::string document(int id){return "d7:records"+(id?"l"+row(id)+"e":std::string("le"))+"6:future"+b(std::string("r\0w",3))+"e";}
std::string read(const fs::path& path){std::ifstream input(path,std::ios::binary);check(bool(input),"Fixture read");return {std::istreambuf_iterator<char>(input),{}};}
void write(const fs::path& path,std::string_view value){std::ofstream out(path,std::ios::binary|std::ios::trunc);out.write(value.data(),static_cast<std::streamsize>(value.size()));out.close();check(bool(out)&&::chmod(path.c_str(),0600)==0,"Fixture write");}
struct Area {
  fs::path root;std::optional<s::ProfileLease> lease;
  explicit Area(bool seeded=true){std::string path="/tmp/biglybtc-download-history-XXXXXX";const auto* made=::mkdtemp(path.data());check(made,"Fresh fixture directory");root=made;check(::chmod(root.c_str(),0700)==0,"Private fixture mode");if(seeded){write(root/"dlhistorya.config",document(1));write(root/"dlhistoryd.config",document(2));write(root/"biglybt.config","d24:Download History Enabledi1e6:future4:keepe");}write(root/"payload-sentinel","payload unchanged");lease.emplace(take(s::ProfileLease::acquire(root)));}
  ~Area(){lease.reset();std::error_code ignored;fs::remove_all(root,ignored);}
};
std::map<std::string,std::string> files(const Area& a){std::map<std::string,std::string> out;for(const auto& item:fs::directory_iterator(a.root)){if(item.is_regular_file()&&!item.is_symlink()){out.emplace(item.path().filename().string(),read(item.path()));}}return out;}
std::unique_ptr<s::DownloadHistoryStore> opened(Area& a){return take(s::DownloadHistoryStore::open(*a.lease));}
void noCreationOnReadNoopOrMissingReceipt(){
  Area a(false);const auto before=files(a);const auto epoch=a.lease->sourceRevision();auto store=opened(a);auto snapshot=take(store->snapshot());check(snapshot.enabled()&&snapshot.writable()&&snapshot.missing(Active)&&snapshot.missing(Dead),"Missing/default history projection");
  check(!take(store->apply(snapshot,Active,c::DownloadHistoryErase{999})).changed&&!take(store->clear(snapshot)).changed,"No-op falsely saved");
  c::DownloadHistoryNewRecord made;made.uid=1;made.name="not a live receipt";error(store->apply(snapshot,Active,c::DownloadHistoryAppend{made}),s::ErrorCode::Unsupported);
  check(files(a)==before&&a.lease->sourceRevision()==epoch,"Read/no-op created history or source effect");take(store->close());
}
void realPrimaryEditsPreserveRawAndPublishTruthfully(){
  Area a;auto store=opened(a);const auto before=files(a);auto snapshot=take(store->snapshot());check(snapshot.enabled()&&store->sharesOwnership(*a.lease),"Actual retained history ownership absent");
  auto changed=take(store->apply(snapshot,Active,c::DownloadHistorySetName{1,"new"}));check(changed.changed&&changed.snapshot.generation()>snapshot.generation()&&changed.snapshot.history(Active)->records[0].name=="new","Actual primary edit missing");
  check(read(a.root/"dlhistorya.config.bak")==before.at("dlhistorya.config")&&read(a.root/"dlhistoryd.config")==before.at("dlhistoryd.config")&&read(a.root/"biglybt.config")==before.at("biglybt.config")&&!fs::exists(a.root/Journal),"Preimage/sibling/settings/intent postconditions");
  const auto parsedOriginal=c::readSavedDownloadHistory(before.at("dlhistorya.config"));check(bool(parsedOriginal),"Fixture core history projection");const auto& original=*parsedOriginal;const auto* old=original.source->find("records");const auto& actual=*changed.snapshot.history(Active);const auto* now=actual.source->find("records");
  for(const auto key:{"u","s","a","c","r","future"}){const auto* x=old->children[0].find(key);const auto* y=now->children[0].find(key);check(x&&y&&original.source->originalBytes().substr(x->begin,x->end-x->begin)==actual.source->originalBytes().substr(y->begin,y->end-y->begin),"Unknown/unselected history field changed");}
  const auto saved=files(a);const auto epoch=a.lease->sourceRevision();check(!take(store->apply(changed.snapshot,Active,c::DownloadHistorySetName{1,"new"})).changed&&files(a)==saved&&epoch==a.lease->sourceRevision(),"Same-name no-op rewrote files");error(store->apply(snapshot,Active,c::DownloadHistoryErase{1}),s::ErrorCode::Conflict);
  const auto cleared=take(store->clear(changed.snapshot));check(cleared.changed&&cleared.snapshot.history(Active)->records.empty()&&cleared.snapshot.history(Dead)->records.empty()&&!fs::exists(a.root/Journal),"Real both-file clear did not complete");take(store->close());auto reopened=opened(a);check(take(reopened->snapshot()).history(Active)->records.empty()&&read(a.root/"payload-sentinel")==before.at("payload-sentinel"),"Reopen/payload preservation failed");
}
void sourceCasAndPrimaryOnlyRecovery(){
  for(unsigned test=0;test<7;++test){Area a;auto store=opened(a);auto snap=take(store->snapshot());
    if(test==0){write(a.root/"dlhistorya.config",document(3));}if(test==1){write(a.root/"dlhistoryd.config",document(3));}if(test==2){write(a.root/"biglybt.config","d24:Download History Enabledi0ee");}if(test==3){write(a.root/"dlhistorya.config.bak","opaque bounded backup");}if(test==4){write(a.root/"dlhistoryd.config.saving",document(3));}if(test==5){write(a.root/Journal,"foreign review marker");}if(test==6){const auto bytes=read(a.root/"dlhistorya.config");fs::rename(a.root/"dlhistorya.config",a.root/"retired");write(a.root/"dlhistorya.config",bytes);}
    const auto before=files(a);check(!store->apply(snap,Active,c::DownloadHistorySetName{1,"blocked"}),"Changed source authorized history overwrite");check(files(a)==before,"CAS refusal changed external bytes");
  }
  {Area a;fs::rename(a.root/"dlhistorya.config",a.root/"dlhistorya.config.bak");const auto before=files(a);error(s::DownloadHistoryStore::open(*a.lease),s::ErrorCode::Conflict);check(files(a)==before,"Fallback was accepted/repaired");}
  {Area a;write(a.root/"dlhistorya.config","bad");write(a.root/"dlhistorya.config.bak",document(1));const auto before=files(a);error(s::DownloadHistoryStore::open(*a.lease),s::ErrorCode::InvalidFormat);check(files(a)==before,"Malformed primary repaired from backup");}
  {Area a;write(a.root/"dlhistorya.config.saving",document(4));auto store=opened(a);const auto snap=take(store->snapshot());check(!snap.writable(),"Pending candidate made writable");const auto before=files(a);error(store->apply(snap,Active,c::DownloadHistorySetName{1,"blocked"}),s::ErrorCode::Conflict);check(files(a)==before,"Pending candidate overwritten");}
  {Area a;write(a.root/"dlhistoryd.config",document(1));auto store=opened(a);check(take(store->snapshot()).review()==s::DownloadHistoryReadOnly::DuplicatePairUid,"Cross-file duplicate UID silently resolved");}
}
void disabledDoesNotAutoClearButExplicitHistoryDeleteWorks(){
  Area a;write(a.root/"biglybt.config","d24:Download History Enabledi0e6:future4:keepe");const auto before=files(a);auto store=opened(a);auto snap=take(store->snapshot());check(!snap.enabled()&&snap.history(Active)->records.size()==1&&files(a)==before,"Disabled read cleared historical files");
  error(store->apply(snap,Active,c::DownloadHistorySetName{1,"must not update"}),s::ErrorCode::Unsupported);check(files(a)==before,"Disabled update wrote files");const auto erased=take(store->apply(snap,Active,c::DownloadHistoryErase{1}));check(erased.changed&&erased.snapshot.history(Active)->records.empty()&&read(a.root/"biglybt.config")==before.at("biglybt.config"),"Explicit erase toggled enabled");
  take(store->close());write(a.root/"biglybt.config","d24:Download History Enabledi2ee");error(s::DownloadHistoryStore::open(*a.lease),s::ErrorCode::InvalidFormat);
}
unsigned crashAt=0;
void crash(s::DownloadHistoryWriteStage stage,std::size_t count){if((crashAt==0&&stage==s::DownloadHistoryWriteStage::IntentDurable)||(stage==s::DownloadHistoryWriteStage::FilePublished&&count==crashAt)){::_exit(static_cast<int>(41+crashAt));}}
void realInterruptedPrefixesRemainReviewOnly(){
  for(unsigned point=0;point<3;++point){Area a;const auto before=files(a);const auto parent=::getpid();const auto child=::fork();check(child>=0,"History prefix fork");if(child==0){if(::prctl(PR_SET_PDEATHSIG,SIGKILL)!=0||::getppid()!=parent){::_exit(123);}crashAt=point;try{auto store=opened(a);(void)store->clear(take(store->snapshot()),{},std::chrono::steady_clock::now()+std::chrono::seconds(10),crash);}catch(...){::_exit(121);}::_exit(122);}
    int status=0;check(::waitpid(child,&status,0)==child&&WIFEXITED(status)&&WEXITSTATUS(status)==static_cast<int>(41+point),"Actual prefix not reached");check(fs::exists(a.root/Journal),"Interrupted prefix lacks intent");
    const auto retained=files(a);auto store=opened(a);const auto snap=take(store->snapshot());check(!snap.writable()&&snap.review()==s::DownloadHistoryReadOnly::PendingIntent,"Prefix silently replayed/writable");
    check(snap.history(Dead)->records.empty()==(point>=1)&&snap.history(Active)->records.empty()==(point>=2),"Actual dead-first prefix differs");if(point<2){error(store->apply(snap,Active,c::DownloadHistorySetName{1,"blocked"}),s::ErrorCode::Conflict);}check(files(a)==retained&&read(a.root/"payload-sentinel")==before.at("payload-sentinel"),"Review modified prefix/payload");
  }
}
std::stop_source* stopSource=nullptr;bool cancelAfterLast=false;
void cancel(s::DownloadHistoryWriteStage stage,std::size_t count){if((!cancelAfterLast&&stage==s::DownloadHistoryWriteStage::IntentDurable)||(cancelAfterLast&&stage==s::DownloadHistoryWriteStage::FilePublished&&count==2)){stopSource->request_stop();}}
void cancellationAndWorkerLeaseLifetimes(){
  {Area a;auto store=opened(a);auto snap=take(store->snapshot());std::stop_source stop;stop.request_stop();const auto before=files(a);error(store->clear(snap,stop.get_token()),s::ErrorCode::Cancelled);error(store->clear(snap,{},s::DownloadHistoryStore::Deadline::min()),s::ErrorCode::Cancelled);check(files(a)==before,"Pre-cancel touched files");
    std::optional<s::ErrorCode> foreign;std::thread thread([&]{const auto result=store->close();if(!result){foreign=result.error().code;}});thread.join();check(foreign==s::ErrorCode::Unsupported,"Foreign close accepted");auto other=opened(a);error(other->apply(snap,Active,c::DownloadHistoryErase{1}),s::ErrorCode::Conflict);take(other->close());other.reset();a.lease.reset();error(s::ProfileLease::acquire(a.root),s::ErrorCode::Busy);take(store->close());store.reset();auto released=s::ProfileLease::acquire(a.root);check(bool(released)&&snap.history(Active)->records[0].name=="old","Snapshot retained OFD or lost owning values");}
  for(bool late:{false,true}){Area a;auto store=opened(a);auto snap=take(store->snapshot());std::stop_source stop;stopSource=&stop;cancelAfterLast=late;const auto result=store->clear(snap,stop.get_token(),s::DownloadHistoryStore::Deadline::max(),cancel);if(late){check(result&&result->changed&&!fs::exists(a.root/Journal),"Completed clear misreported late cancellation");}else{error(result,s::ErrorCode::Cancelled);check(store->recoveryRequired()&&fs::exists(a.root/Journal),"Partial cancellation lost review intent");}stopSource=nullptr;}
}
struct rlimit oldLimit{};struct sigaction oldSignal{};
void failAfterIntent(s::DownloadHistoryWriteStage stage,std::size_t){if(stage==s::DownloadHistoryWriteStage::IntentDurable){auto value=oldLimit;value.rlim_cur=32;check(::setrlimit(RLIMIT_FSIZE,&value)==0,"File fault setup");}}
void actualFileFailureKeepsReviewAndExternalPathsRefuse(){
  {Area a;auto store=opened(a);const auto snap=take(store->snapshot());const auto before=files(a);check(::getrlimit(RLIMIT_FSIZE,&oldLimit)==0,"Resource limit read");struct sigaction ignore{};ignore.sa_handler=SIG_IGN;sigemptyset(&ignore.sa_mask);check(::sigaction(SIGXFSZ,&ignore,&oldSignal)==0,"Resource signal setup");
    struct Restore{~Restore(){::setrlimit(RLIMIT_FSIZE,&oldLimit);::sigaction(SIGXFSZ,&oldSignal,nullptr);}} restore;
    const auto result=store->apply(snap,Active,c::DownloadHistorySetName{1,"will fail actual write"},{},s::DownloadHistoryStore::Deadline::max(),failAfterIntent);check(!result&&store->recoveryRequired(),"Actual I/O failure was Saved/retriable");check(read(a.root/"dlhistorya.config")==before.at("dlhistorya.config")&&fs::exists(a.root/Journal),"Failed write lost source or intent");
  }
  for(bool hard:{false,true}){Area a;write(a.root/"sentinel","unchanged");fs::remove(a.root/"dlhistorya.config");const auto made=hard ? ::link((a.root/"sentinel").c_str(),(a.root/"dlhistorya.config").c_str()) : ::symlink((a.root/"sentinel").c_str(),(a.root/"dlhistorya.config").c_str());check(made==0,"Unsafe path fixture");error(s::DownloadHistoryStore::open(*a.lease),s::ErrorCode::UnsafePath);check(read(a.root/"sentinel")=="unchanged","Unsafe path modified target");}
  {Area a;auto store=opened(a);const auto snap=take(store->snapshot());fs::rename(a.root/".azlock",a.root/"old-lock");write(a.root/".azlock","");const auto before=files(a);check(!store->apply(snap,Active,c::DownloadHistoryErase{1})&&files(a)==before,"Changed lock source accepted");}
  {Area a;auto store=opened(a);const auto snap=take(store->snapshot());const auto moved=fs::path(a.root.string()+"-held");fs::rename(a.root,moved);fs::create_directory(a.root);write(a.root/"sentinel","new named directory");
    struct Restore{fs::path path,held;~Restore(){std::error_code error;fs::remove_all(path,error);fs::rename(held,path,error);}} restore{a.root,moved};
    check(!store->apply(snap,Active,c::DownloadHistoryErase{1})&&read(a.root/"sentinel")=="new named directory"&&read(moved/"dlhistorya.config")==document(1),"Renamed profile wrote held/new directory");take(store->close());}
}
s::DownloadHistoryStore* observedStore=nullptr;const s::ProfileLease* observedLease=nullptr;bool reentryRejected=false;
void cooperativeChange(s::DownloadHistoryWriteStage stage,std::size_t){if(stage==s::DownloadHistoryWriteStage::IntentDurable){const auto current=observedStore->snapshot();reentryRejected=!current&&current.error().code==s::ErrorCode::Conflict;auto source=take(observedLease->sourceAccess(true));auto config=take(s::ConfigFile::load(*observedLease,"biglybt.config"));check(bool(config.document().replaceValue("future","7:changed")),"Cooperative settings edit");take(config.save(*observedLease));}}
void cooperativeSourceChangesAcrossOutsideObserverAreNotOverwritten(){
  Area a;auto store=opened(a);const auto snap=take(store->snapshot());const auto before=read(a.root/"dlhistorya.config");observedStore=store.get();observedLease=&*a.lease;reentryRejected=false;
  const auto result=store->apply(snap,Active,c::DownloadHistorySetName{1,"not published"},{},s::DownloadHistoryStore::Deadline::max(),cooperativeChange);error(result,s::ErrorCode::Conflict);check(reentryRejected&&store->recoveryRequired()&&read(a.root/"dlhistorya.config")==before&&read(a.root/"biglybt.config").find("7:changed")!=std::string::npos&&fs::exists(a.root/Journal),"Source conflict/reentry did not preserve external publication");observedStore=nullptr;observedLease=nullptr;
}
}
int main(){try{noCreationOnReadNoopOrMissingReceipt();realPrimaryEditsPreserveRawAndPublishTruthfully();sourceCasAndPrimaryOnlyRecovery();disabledDoesNotAutoClearButExplicitHistoryDeleteWorks();realInterruptedPrefixesRemainReviewOnly();cancellationAndWorkerLeaseLifetimes();actualFileFailureKeepsReviewAndExternalPathsRefuse();cooperativeSourceChangesAcrossOutsideObserverAreNotOverwritten();std::cout<<"PASS owning download-history store: eight generated-profile families\n";return 0;}catch(const std::exception& failure){std::cerr<<failure.what()<<'\n';return 1;}}
