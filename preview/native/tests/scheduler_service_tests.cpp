#include "scheduler_service.hpp"
#include "scheduler_io.hpp"
#include "ownership_file.hpp"
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <span>
#include <stdexcept>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace fs=std::filesystem;
namespace st=biglybtc::storage;
namespace core=biglybtc::core;
namespace policy=biglybtc::plugins::speed_scheduler;
namespace {
void check(bool value,const char* reason){if(!value){throw std::runtime_error(reason);}}
template<class T> T take(std::expected<T,st::Error>&& result){if(!result){throw std::runtime_error(result.error().message);}return std::move(*result);}
void done(std::expected<void,st::Error>&& result){if(!result){throw std::runtime_error(result.error().message);}}
std::string read(const fs::path& path){std::ifstream file(path,std::ios::binary);check(bool(file),"fixture read");return {std::istreambuf_iterator<char>(file),{}};}
void write(const fs::path& path,std::string_view value){fs::create_directories(path.parent_path());std::ofstream file(path,std::ios::binary);file.write(value.data(),static_cast<std::streamsize>(value.size()));check(bool(file),"fixture write");}
struct Area {
  fs::path root;
  Area(){char name[]="/tmp/biglybtc-scheduler-XXXXXX";auto* value=::mkdtemp(name);check(value,"mkdtemp");root=value;}
  ~Area(){std::error_code ec;fs::remove_all(root,ec);}
  fs::path profile(){auto path=root/"profile";fs::create_directories(path);return path;}
};
std::string bstring(std::string_view value){return std::to_string(value.size())+":"+std::string(value);}
std::string config(std::string_view leaf,bool enabled=true){
  return "d"+bstring("Plugin."+std::string(leaf)+".speedscheduler.enabled")+(enabled?"i1e":"i0e")+bstring("opaque")+"d4:keep3:rawee";
}
const std::string Xml="<?xml version=\"1.0\"?><SpeedScheduler opaque=\"keep\"><!--kept--><defaultRates upload=\"7\" download=\"9\" enforce=\"1\"/><schedules><schedule enabled=\"true\" extra=\"keep\"><startTime hour=\"0\" minute=\"0\"/><endTime hour=\"23\" minute=\"59\"/><rates upload=\"17\" download=\"19\" pauseDownloads=\"true\" pauseSeeds=\"false\"/><days><day index=\"0\" selected=\"true\"/></days><category selected=\"Uncategorized\"/><catSelection not_in=\"false\" in=\"false\"/><future raw=\"still here\"/></schedule></schedules></SpeedScheduler>";
core::TorrentHash hash(){auto value=core::TorrentHash::parse(std::string(20,'x'));check(bool(value),"hash parse");return *value;}
st::SchedulerTorrent torrent(){return {.token=71,.manualRevision=3,.hash=hash(),.state=policy::TorrentState::Downloading,.memberships={"Uncategorized"}};}
st::SchedulerModuleApproval approval(const fs::path& module){
  st::detail::Descriptor fd(::open(module.c_str(),O_RDONLY|O_CLOEXEC));check(fd.get()>=0,"module fixture open");
  return {st::scheduler_detail::sha256(fd.get(),fs::file_size(module)),fs::file_size(module),"0.1.0"};
}
struct Fixture {
  Area area;fs::path profile;st::ProfileLease lease;fs::path module;st::SchedulerModuleApproval approved;
  explicit Fixture(const fs::path& input):profile(area.profile()),lease(take(st::ProfileLease::acquire(profile))),module(area.root/"scheduler.so"){
    fs::copy_file(input,module);fs::permissions(module,fs::perms::owner_read|fs::perms::owner_write|fs::perms::owner_exec);approved=approval(module);
  }
  void original(std::string_view leaf="SpeedScheduler",bool descriptor=true){
    const auto dir=profile/"plugins"/leaf;fs::create_directories(dir);
    if(descriptor){write(dir/"plugin.properties","plugin.id = SpeedScheduler\nplugin.version=1.8\n");}else{write(dir/"SpeedScheduler_1.8.jar","opaque generated jar placeholder; never loaded");}
    write(dir/"SavedSchedules.xml",Xml);write(profile/"biglybt.config",config(leaf));
  }
  std::unique_ptr<st::SchedulerService> open(){return take(st::loadApprovedSchedulerService(lease,module,approved));}
};
void losslessXml(){
  st::SchedulerXmlChanges none;check(take(st::patchSchedulerXml(Xml,none))==Xml,"XML no-op byte exact");
  auto parsed=policy::readOriginalXml(Xml);check(bool(parsed),"original literal parsed");auto schedule=parsed->configuration.schedules.front();schedule.upload=23;schedule.category="A & \"B\"";
  st::SchedulerXmlChanges edits;edits.update.push_back({0,schedule});auto changed=take(st::patchSchedulerXml(Xml,edits));
  check(changed.find("<!--kept-->")!=changed.npos&&changed.find("<future raw=\"still here\"/>")!=changed.npos&&changed.find("extra=\"keep\"")!=changed.npos,"unknown XML bytes preserved");
  auto reparsed=policy::readOriginalXml(changed);check(reparsed&&reparsed->configuration.schedules.front()==schedule,"requested XML model exact");
  edits.update.push_back({0,schedule});check(!st::patchSchedulerXml(Xml,edits),"duplicate edit rejects");
  st::SchedulerXmlChanges append;append.append.push_back(schedule);auto added=take(st::patchSchedulerXml(Xml,append));check(policy::readOriginalXml(added)->configuration.schedules.size()==2,"append schedule");
  st::SchedulerXmlChanges remove;remove.remove.push_back(0);check(policy::readOriginalXml(take(st::patchSchedulerXml(Xml,remove)))->configuration.schedules.empty(),"explicit removal");
  schedule.start=-1;append.append={schedule};check(!st::patchSchedulerXml(Xml,append),"invalid appended time rejects");
  std::string utf16="\xff\xfe";for(const unsigned char c:Xml){utf16+=static_cast<char>(c);utf16+='\0';}
  // Encoding declaration must describe the UTF16 literal, and raw comments are
  // checked against their unchanged UTF16 byte representation.
  check(take(st::patchSchedulerXml(utf16,none))==utf16,"UTF16 no-op byte exact");
  auto edited16=take(st::patchSchedulerXml(utf16,st::SchedulerXmlChanges{.defaults=st::SchedulerXmlChanges::Defaults{13,15,true}}));
  check(edited16.starts_with("\xff\xfe")&&policy::readOriginalXml(edited16)->configuration.defaultUpload==13,"UTF16 sparse update");
}
void resolutionAndSparseConfig(const fs::path& module){
  Fixture f(module);f.original("SpeedScheduler_1.8",false);
  fs::permissions(f.profile/"plugins",fs::perms::owner_all|fs::perms::group_all|fs::perms::others_read|fs::perms::others_exec);
  fs::permissions(f.profile/"plugins"/"SpeedScheduler_1.8",fs::perms::owner_all|fs::perms::group_all|fs::perms::others_read|fs::perms::others_exec);
  const auto initial=read(f.profile/"biglybt.config");auto service=f.open();
  check(service->snapshot().directoryLeaf=="SpeedScheduler_1.8","jar-only data/config selects actual directory");
  const auto before=service->snapshot().revision;done(service->saveSettings(f.lease,before,{.overrideForced=true}));
  auto document=core::Document::parse(read(f.profile/"biglybt.config"));check(document&&document->find("opaque")&&document->find("opaque")->find("keep")->bytes=="raw","opaque config retained");
  check(document->find("Plugin.SpeedScheduler_1.8.override.forced")->integer==1&&!document->find("Plugin.SpeedScheduler.thread.sleep.time"),"sparse actual-directory setting only");
  check(!service->saveSettings(f.lease,before,{.enabled=false}),"stale editor rejects");check(initial!=read(f.profile/"biglybt.config"),"explicit change persisted");
  write(f.profile/"plugins"/"Other"/"plugin.properties","plugin.id: SpeedScheduler\n");check(!service->reload(f.lease),"ambiguous descriptors reject");
}
void creationAndCancellation(const fs::path& module){
  Fixture f(module);auto service=f.open();check(!fs::exists(f.profile/"plugins"),"read missing scheduler creates no directory");
  std::stop_source stop;stop.request_stop();check(!service->saveSchedules(f.lease,service->snapshot().revision,{},stop.get_token()),"cancel before mkdir");check(!fs::exists(f.profile/"plugins"),"cancel no mutation");
  done(service->saveSchedules(f.lease,service->snapshot().revision,{}));check(fs::exists(f.profile/"plugins"/"SpeedScheduler"/"BiglyBTC.scheduler-data"),"created data marker");
  service.reset();service=f.open();check(service->snapshot().configuration.has_value(),"native data-only directory reopens without fake descriptor");
  check(!service->snapshot().settings.enabled,"absent enabled key is source-default false");
}
void actualModuleAndOwnership(const fs::path& module){
  Fixture f(module);f.original();auto service=f.open();auto item=torrent();
  const auto ownership=policy::originalOwnershipHash({reinterpret_cast<const std::uint8_t*>(item.hash.bytes().data()),item.hash.bytes().size()});check(bool(ownership),"original ownership format");
  write(f.profile/"plugins"/"SpeedScheduler"/"PausedTorrents.conf",*ownership+"\r\nopaque line\r\n");done(service->reload(f.lease));
  item.state=policy::TorrentState::Stopped;item.paused=true;
  auto inherited=take(service->evaluate({0,1},std::span(&item,1),7,9));check(inherited.actions.empty(),"legacy hash file never grants resume authority");
  item.state=policy::TorrentState::Downloading;item.paused=false;auto plan=take(service->evaluate({0,1},std::span(&item,1),7,9));
  check(plan.uploadKiB==17&&plan.downloadKiB==19&&plan.actions.size()==1&&plan.actions.front().kind==policy::ActionKind::Pause,"actual authenticated module evaluates literal rule");
  auto stale=item;++stale.manualRevision;check(!service->beginAction(f.lease,plan.actions.front(),stale),"manual revision blocks stale action");
  done(service->beginAction(f.lease,plan.actions.front(),item));check(fs::exists(f.profile/"plugins"/"SpeedScheduler"/"BiglyBTC.scheduler-intent"),"intent precedes engine callback");
  done(service->finishAction(f.lease,plan.actions.front(),st::SchedulerActionResult::Succeeded));check(!fs::exists(f.profile/"plugins"/"SpeedScheduler"/"BiglyBTC.scheduler-intent"),"acknowledged action clears synced intent");
  done(service->saveSettings(f.lease,service->snapshot().revision,{.enabled=false}));item.state=policy::TorrentState::Stopped;item.paused=true;
  auto resume=take(service->evaluate({0,1},std::span(&item,1),17,19));check(resume.actions.size()==1&&resume.actions.front().kind==policy::ActionKind::Resume&&resume.uploadKiB==7&&resume.downloadKiB==9,"disable restores defaults and resumes only acknowledged pause");
  done(service->beginAction(f.lease,resume.actions.front(),item));done(service->finishAction(f.lease,resume.actions.front(),st::SchedulerActionResult::Succeeded));
  check(read(f.profile/"plugins"/"SpeedScheduler"/"PausedTorrents.conf")=="opaque line\r\n","unknown ownership line stays byte exact");
}
void interruptionAndManualPriority(const fs::path& module){
  Fixture f(module);f.original();auto service=f.open();auto item=torrent();auto plan=take(service->evaluate({0,5},std::span(&item,1),7,9));
  done(service->beginAction(f.lease,plan.actions.front(),item));service.reset();service=f.open();
  check(service->snapshot().interruptedAction&&!service->evaluate({0,5},std::span(&item,1),7,9),"restart retains journal and never retries uncertain engine effect");
  done(service->revokeForManual(f.lease,item.hash));check(!fs::exists(f.profile/"plugins"/"SpeedScheduler"/"BiglyBTC.scheduler-intent"),"explicit manual override clears exact interrupted intent");
  plan=take(service->evaluate({0,5},std::span(&item,1),7,9));done(service->beginAction(f.lease,plan.actions.front(),item));done(service->finishAction(f.lease,plan.actions.front(),st::SchedulerActionResult::Succeeded));
  item.state=policy::TorrentState::Stopped;item.paused=true;done(service->saveSettings(f.lease,service->snapshot().revision,{.enabled=false}));
  auto owned=take(service->evaluate({0,5},std::span(&item,1),17,19));check(owned.actions.size()==1,"ownership positive before manual override");
  write(f.profile/"plugins"/"SpeedScheduler"/"PausedTorrents.conf","external edit\n");
  check(!service->revokeForManual(f.lease,item.hash),"external CAS prevents destructive revoke rewrite");
  auto after=take(service->evaluate({0,5},std::span(&item,1),17,19));check(after.actions.empty(),"manual authority revoked even when durable save fails");
}
void pausedAdmissionRebind(const fs::path& module){
  Fixture f(module);f.original();auto service=f.open();auto item=torrent();
  const auto owner=f.profile/"plugins"/"SpeedScheduler"/"PausedTorrents.conf";
  const auto intent=owner.parent_path()/"BiglyBTC.scheduler-intent";
  const auto encoded=policy::originalOwnershipHash({reinterpret_cast<const std::uint8_t*>(item.hash.bytes().data()),item.hash.bytes().size()});
  check(bool(encoded),"ownership hash fixture");write(owner,*encoded+"\r\nopaque line\r\n");done(service->reload(f.lease));
  const auto inherited=read(owner);
  check(!take(service->rebindOwnedPause(f.lease,item.hash,item.token,item.token+1,item.manualRevision)),"legacy hash alone cannot transfer authority");
  check(read(owner)==inherited,"legacy rejection does not rewrite original ownership");
  auto pause=take(service->evaluate({0,5},std::span(&item,1),7,9));check(pause.actions.size()==1,"pause plan fixture");
  done(service->beginAction(f.lease,pause.actions.front(),item));done(service->finishAction(f.lease,pause.actions.front(),st::SchedulerActionResult::Succeeded));
  done(service->saveSettings(f.lease,service->snapshot().revision,{.enabled=false}));
  item.paused=true;item.state=policy::TorrentState::Stopped;
  const auto issued=take(service->evaluate({0,5},std::span(&item,1),17,19));check(issued.actions.size()==1,"old paused admission has acknowledged resume authority");
  const auto bytes=read(owner);struct stat before{};check(::stat(owner.c_str(),&before)==0,"ownership before identity");
  check(!service->rebindOwnedPause(f.lease,item.hash,item.token+1,item.token+2,item.manualRevision),"wrong old token rejects");
  check(!service->rebindOwnedPause(f.lease,item.hash,item.token,item.token+1,item.manualRevision+1),"wrong manual revision rejects");
  check(!service->rebindOwnedPause(f.lease,item.hash,item.token,item.token,item.manualRevision),"nonadvancing token rejects");
  write(intent,"unacknowledged external intent\n");
  check(!service->rebindOwnedPause(f.lease,item.hash,item.token,item.token+1,item.manualRevision),"unexpected durable intent prevents transfer");fs::remove(intent);
  const auto revision=service->snapshot().revision;
  check(take(service->rebindOwnedPause(f.lease,item.hash,item.token,item.token+1,item.manualRevision)),"existing acknowledged pause transfers to actual new admission");
  check(service->snapshot().revision>revision&&!service->beginAction(f.lease,issued.actions.front(),item),"transfer invalidates every old issued action");
  check(!service->acknowledgeRates(issued.id,true,true),"old rate acknowledgement is stale after transfer");
  struct stat after{};check(::stat(owner.c_str(),&after)==0&&before.st_dev==after.st_dev&&before.st_ino==after.st_ino
      &&before.st_mtim.tv_sec==after.st_mtim.tv_sec&&before.st_mtim.tv_nsec==after.st_mtim.tv_nsec
      &&before.st_ctim.tv_sec==after.st_ctim.tv_sec&&before.st_ctim.tv_nsec==after.st_ctim.tv_nsec
      &&read(owner)==bytes,"transfer preserves original ownership bytes and inode timestamps");
  auto old=take(service->evaluate({0,5},std::span(&item,1),17,19));check(old.actions.empty(),"old admission no longer owns pause");
  ++item.token;auto resumed=take(service->evaluate({0,5},std::span(&item,1),17,19));
  check(resumed.actions.size()==1&&resumed.actions.front().kind==policy::ActionKind::Resume,"new admission receives the existing resume authority");
  done(service->beginAction(f.lease,resumed.actions.front(),item));
  check(!service->rebindOwnedPause(f.lease,item.hash,item.token,item.token+1,item.manualRevision),"pending engine acknowledgement blocks transfer");
  done(service->finishAction(f.lease,resumed.actions.front(),st::SchedulerActionResult::FailedBeforeEffect));
  write(owner,bytes+"external edit\n");
  check(!service->rebindOwnedPause(f.lease,item.hash,item.token,item.token+1,item.manualRevision),"ownership whole-source CAS rejects external edits");
  check(read(owner)==bytes+"external edit\n","CAS failure preserves external source bytes");
  done(service->reload(f.lease));
  check(!take(service->rebindOwnedPause(f.lease,item.hash,item.token,item.token+1,item.manualRevision)),"reload of changed legacy source cannot recreate authority");
}
void reviewedInterruptionKeepsStateAndRevokesDuplicates(const fs::path& module){
  Fixture f(module);f.original();auto service=f.open();auto item=torrent();
  const auto dir=f.profile/"plugins"/"SpeedScheduler";const auto journal=dir/"BiglyBTC.scheduler-intent";
  const auto own=dir/"PausedTorrents.conf";
  const auto encoded=policy::originalOwnershipHash({reinterpret_cast<const std::uint8_t*>(item.hash.bytes().data()),item.hash.bytes().size()});
  check(bool(encoded),"source ownership encoding");
  write(own,*encoded+"\r\nopaque survivor\r\n "+*encoded+" \n");done(service->reload(f.lease));
  auto plan=take(service->evaluate({0,5},std::span(&item,1),7,9));
  done(service->beginAction(f.lease,plan.actions.front(),item));
  check(!service->finishAction(f.lease,plan.actions.front(),st::SchedulerActionResult::Indeterminate),"real service retains uncertain acknowledgement");
  done(service->reload(f.lease));check(service->snapshot().interrupted&&service->snapshot().interrupted->hash==item.hash,"strict typed native journal after reload");
  const auto revision=service->snapshot().revision;const auto bytes=read(journal),permission=read(own);
  const auto xml=read(dir/"SavedSchedules.xml"),cfg=read(f.profile/"biglybt.config");
  check(!service->settleInterrupted(f.lease,revision-1,item.hash,bytes),"stale recovery rejected");
  check(!service->settleInterrupted(f.lease,revision,item.hash,bytes+"0"),"different same-hash journal token rejected");
  std::stop_source cancelled;cancelled.request_stop();check(!service->settleInterrupted(f.lease,revision,item.hash,bytes,cancelled.get_token()),"cancel before mutation");
  check(read(journal)==bytes&&read(own)==permission,"rejected review leaves exact sources");
  write(dir/"PausedTorrents.conf.BiglyBTC.saving","uncertain prior output");
  check(!service->settleInterrupted(f.lease,revision,item.hash,bytes),"pending temporary cannot be silently adopted");
  check(read(journal)==bytes&&read(own)==permission,"temporary refusal keeps intent and permission");
  fs::remove(dir/"PausedTorrents.conf.BiglyBTC.saving");
  done(service->settleInterrupted(f.lease,revision,item.hash,bytes));
  check(!fs::exists(journal)&&read(own)=="opaque survivor\r\n","all target duplicates revoked with foreign bytes exact");
  check(read(dir/"SavedSchedules.xml")==xml&&read(f.profile/"biglybt.config")==cfg,"review changes no original schedule/config");
  check(!service->snapshot().interruptedAction&&!service->snapshot().interrupted&&service->snapshot().revision>revision,"only durable settlement publishes resolution");
  check(!service->finishAction(f.lease,plan.actions.front(),st::SchedulerActionResult::Succeeded),"old callback cannot acknowledge reviewed action");
  item.paused=true;item.state=policy::TorrentState::Stopped;
  done(service->saveSettings(f.lease,service->snapshot().revision,{.enabled=false}));
  auto next=take(service->evaluate({0,5},std::span(&item,1),17,19));check(next.actions.empty(),"settlement never mints current-session resume authority");
}
void importedAndUnknownJournalReview(const fs::path& module){
  Fixture f(module);f.original();const auto dir=f.profile/"plugins"/"SpeedScheduler";
  const auto journal=dir/"BiglyBTC.scheduler-intent",own=dir/"PausedTorrents.conf";const auto item=torrent();
  const auto encoded=policy::originalOwnershipHash({reinterpret_cast<const std::uint8_t*>(item.hash.bytes().data()),item.hash.bytes().size()});check(bool(encoded),"imported hash encoding");
  // Literal native journal fixture, not a fabricated original/engine receipt.
  const auto bytes="BiglyBTC Scheduler imported intent 1\n"+item.hash.hex()+"\n11\n0\n1\n";
  write(journal,bytes);write(own,*encoded+"\nforeign permission text\n");auto service=f.open();
  check(service->snapshot().interrupted&&service->snapshot().interrupted->imported,"recognized imported source is distinct");
  write(f.profile/"biglybt.config",config("SpeedScheduler",false));
  check(!service->settleInterrupted(f.lease,service->snapshot().revision,item.hash,bytes),"whole config CAS before recovery mutation");
  check(read(journal)==bytes&&read(own)==*encoded+"\nforeign permission text\n","external config conflict cannot consume permission");
  done(service->reload(f.lease));done(service->settleInterrupted(f.lease,service->snapshot().revision,item.hash,bytes));
  check(read(own)=="foreign permission text\n"&&!fs::exists(journal),"imported permission discarded without inferred success");
  const auto future="BiglyBTC Scheduler imported intent 2\n"+item.hash.hex()+"\n11\n0\n1\n";
  write(journal,future);done(service->reload(f.lease));
  check(service->snapshot().interruptedAction&&!service->snapshot().interrupted,"future journal is unavailable, not guessed");
  check(!service->settleInterrupted(f.lease,service->snapshot().revision,item.hash,bytes),"unknown version cannot be removed by reviewed hash");
  check(read(journal)==future&&read(own)=="foreign permission text\n","unknown source remains byte exact");
}
void permissionFirstRecoveryCanBeReviewedAgain(const fs::path& module){
  Fixture f(module);f.original();const auto dir=f.profile/"plugins"/"SpeedScheduler";const auto item=torrent();
  // Durable state after permission publication but before intent unlink. No
  // success receipt or engine state is invented by this restart fixture.
  write(dir/"PausedTorrents.conf","foreign survivor\r\n");
  write(dir/"BiglyBTC.scheduler-intent","BiglyBTC Scheduler intent 1\n"+item.hash.hex()+"\n71\n3\n0\n");
  auto service=f.open();const auto revision=service->snapshot().revision;const auto bytes=read(dir/"BiglyBTC.scheduler-intent");
  struct stat before{},after{};check(::stat((dir/"PausedTorrents.conf").c_str(),&before)==0,"inspect already-revoked permission");
  done(service->settleInterrupted(f.lease,revision,item.hash,bytes));
  check(::stat((dir/"PausedTorrents.conf").c_str(),&after)==0&&before.st_dev==after.st_dev&&before.st_ino==after.st_ino,"already-revoked permission not rewritten");
  check(read(dir/"PausedTorrents.conf")=="foreign survivor\r\n"&&!fs::exists(dir/"BiglyBTC.scheduler-intent"),"explicit restart review only completes cleanup");
  check(!service->settleInterrupted(f.lease,revision,item.hash,bytes),"completed settlement is not replayed");
}
void unsafeAndAuthentication(const fs::path& module){
  Fixture world(module);world.original();fs::permissions(world.profile/"plugins",fs::perms::all);
  const auto denied=st::loadApprovedSchedulerService(world.lease,world.module,world.approved);
  check(!denied&&denied.error().code==st::ErrorCode::UnsafePath,"world-writable plugin data root still rejects");
  Fixture f(module);f.original();auto bad=f.approved;bad.sha256[0]=bad.sha256[0]=='0'?'1':'0';check(!st::loadApprovedSchedulerService(f.lease,f.module,bad),"bad approval rejects before native load");
  auto service=f.open();write(f.profile/"plugins"/"SpeedScheduler"/"SavedSchedules.xml",Xml+"\n");
  check(!service->saveSchedules(f.lease,service->snapshot().revision,{}),"XML CAS detects external bytes");
  Fixture unsafe(module);unsafe.original();fs::remove(unsafe.profile/"plugins"/"SpeedScheduler"/"SavedSchedules.xml");check(::mkfifo((unsafe.profile/"plugins"/"SpeedScheduler"/"SavedSchedules.xml").c_str(),0600)==0,"fifo setup");
  check(!st::loadApprovedSchedulerService(unsafe.lease,unsafe.module,unsafe.approved),"FIFO rejects without blocking");
  Fixture link(module);link.original();fs::remove(link.profile/"plugins"/"SpeedScheduler"/"SavedSchedules.xml");fs::create_symlink(link.module,link.profile/"plugins"/"SpeedScheduler"/"SavedSchedules.xml");check(!st::loadApprovedSchedulerService(link.lease,link.module,link.approved),"symlink rejects");
}
}
int main(int argc,char** argv){
  try{check(argc==2,"usage: scheduler-service-tests approved-module.so");const auto module=fs::absolute(argv[1]);
    losslessXml();resolutionAndSparseConfig(module);creationAndCancellation(module);actualModuleAndOwnership(module);interruptionAndManualPriority(module);pausedAdmissionRebind(module);reviewedInterruptionKeepsStateAndRevokesDuplicates(module);importedAndUnknownJournalReview(module);permissionFirstRecoveryCanBeReviewedAgain(module);unsafeAndAuthentication(module);
    std::cout<<"scheduler service: ten owned-fixture families passed\n";return 0;
  }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
