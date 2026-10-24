#include "scheduler_service.hpp"
#include "scheduler_io.hpp"
#include "ownership_file.hpp"
#include "biglybtc/scheduler.h"
#include <algorithm>
#include <bit>
#include <charconv>
#include <climits>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <limits>
#include <map>
#include <set>
#include <unistd.h>

namespace biglybtc::storage {
namespace {
using namespace scheduler_detail;
constexpr std::size_t XmlMaximum=4*1024*1024,ConfigMaximum=192*1024*1024,IntentMaximum=1024;
constexpr const char* XmlName="SavedSchedules.xml";
constexpr const char* OwnershipName="PausedTorrents.conf";
constexpr const char* IntentName="BiglyBTC.scheduler-intent";
constexpr const char* MarkerName="BiglyBTC.scheduler-data";
constexpr std::string_view Marker="BiglyBTC SpeedScheduler data 1\n";
template<class F> auto guarded(F&& action)->decltype(action()) {
  try{return action();}catch(const Failure& error){return std::unexpected(error.error);}
  catch(const std::bad_alloc&){return std::unexpected(Error{ErrorCode::Io,"Insufficient memory for scheduler service"});}
}
std::span<const std::uint8_t> hashBytes(const core::TorrentHash& hash) {
  const auto bytes=hash.bytes();return {reinterpret_cast<const std::uint8_t*>(bytes.data()),bytes.size()};
}
bool validLeaf(std::string_view name) {
  return !name.empty()&&name.size()<=255&&name!="."&&name!=".."&&name.find('/')==name.npos&&name.find('\0')==name.npos;
}
struct ConfigurationSource { Raw raw; core::Document document; };
ConfigurationSource configuration(const ProfileLease& lease) {
  auto raw=read(lease.directoryHandle(),"biglybt.config",ConfigMaximum);
  auto loaded=ConfigFile::load(lease,"biglybt.config");
  if(!loaded){
    if(loaded.error().code!=ErrorCode::Missing||raw.bytes){throw Failure{loaded.error()};}
    auto empty=core::Document::parse("de");return {std::move(raw),std::move(*empty)};
  }
  if(loaded->recoverySource()!=RecoverySource::Primary||!raw.bytes||loaded->document().originalBytes()!=*raw.bytes){
    fail(ErrorCode::Conflict,"Scheduler configuration needs explicit recovery before use");}
  if(loaded->document().root().type!=core::Node::Type::Dictionary){fail(ErrorCode::InvalidFormat,"Scheduler configuration root must be a dictionary");}
  return {std::move(raw),std::move(loaded->document())};
}
std::string propertyText(std::string_view input) {
  std::string result;
  for(std::size_t i=0;i<input.size();++i){char c=input[i];if(c!='\\'){result+=c;continue;}
    if(++i==input.size()){fail(ErrorCode::InvalidFormat,"Truncated plugin descriptor escape");}c=input[i];
    if(c=='u'){
      if(input.size()-i<5){fail(ErrorCode::InvalidFormat,"Truncated plugin descriptor Unicode escape");}
      unsigned value=0;const auto start=input.data()+i+1;const auto parsed=std::from_chars(start,start+4,value,16);
      if(parsed.ec!=std::errc{}||parsed.ptr!=start+4){fail(ErrorCode::InvalidFormat,"Invalid plugin descriptor Unicode escape");}
      result+=value<128?static_cast<char>(value):'?';i+=4;
    }else{result+=c=='t'?'\t':c=='n'?'\n':c=='r'?'\r':c=='f'?'\f':c;}
  }return result;
}
bool descriptorMatches(std::string_view bytes) {
  std::optional<std::string> id;std::string logical;std::size_t pos=0;
  while(pos<bytes.size()){
    const auto end=bytes.find_first_of("\r\n",pos);auto line=bytes.substr(pos,end==bytes.npos?bytes.size()-pos:end-pos);
    pos=end==bytes.npos?bytes.size():end+1;if(end!=bytes.npos&&bytes[end]=='\r'&&pos<bytes.size()&&bytes[pos]=='\n'){++pos;}
    if(!logical.empty()){const auto start=line.find_first_not_of(" \t\f");line=start==line.npos?std::string_view{}:line.substr(start);}
    logical+=line;std::size_t slashes=0;for(auto i=logical.size();i>0&&logical[i-1]=='\\';--i){++slashes;}
    if(slashes%2){logical.pop_back();continue;}
    auto value=std::string_view(logical);const auto first=value.find_first_not_of(" \t\f");
    if(first==value.npos||value[first]=='#'||value[first]=='!'){logical.clear();continue;}value.remove_prefix(first);
    std::size_t split=0;bool escaped=false;
    for(;split<value.size();++split){const char c=value[split];if(!escaped&&(c=='='||c==':'||c==' '||c=='\t'||c=='\f')){break;}if(c=='\\'){escaped=!escaped;}else{escaped=false;}}
    auto key=propertyText(value.substr(0,split));auto rest=value.substr(split);const auto v=rest.find_first_not_of(" \t\f");rest=v==rest.npos?std::string_view{}:rest.substr(v);
    if(!rest.empty()&&(rest[0]=='='||rest[0]==':')){rest.remove_prefix(1);const auto s=rest.find_first_not_of(" \t\f");rest=s==rest.npos?std::string_view{}:rest.substr(s);}
    if(key=="plugin.id"){if(id){fail(ErrorCode::InvalidFormat,"Duplicate plugin.id descriptor key");}id=propertyText(rest);}logical.clear();
  }
  if(!logical.empty()){fail(ErrorCode::InvalidFormat,"Unterminated descriptor continuation");}
  return id==std::optional<std::string>{"SpeedScheduler"};
}
bool hasPluginKey(const core::Document& config,std::string_view leaf) {
  const auto prefix="Plugin."+std::string(leaf)+".";
  for(const auto* suffix:{"speedscheduler.enabled","use.tags.not.cats","override.forced","thread.sleep.time"}){
    if(config.find(prefix+suffix)){return true;}}
  return false;
}
bool regularEvidence(int parent,const char* name,std::size_t maximum) {
  struct stat info{};if(::fstatat(parent,name,&info,AT_SYMLINK_NOFOLLOW)!=0){if(errno==ENOENT){return false;}io("Cannot inspect scheduler data evidence");}
  if(!S_ISREG(info.st_mode)||info.st_uid!=::geteuid()||info.st_nlink!=1||info.st_size<0){fail(ErrorCode::UnsafePath,"Scheduler data evidence must be an owned regular file");}
  if(static_cast<std::uint64_t>(info.st_size)>maximum){fail(ErrorCode::InvalidFormat,"Scheduler data evidence exceeds bound");}return true;
}
std::string locate(int plugins,const core::Document& config) {
  if(plugins<0){return "SpeedScheduler";}
  detail::Descriptor copy(::openat(plugins,".",O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC));if(copy.get()<0){io("Cannot enumerate plugin data directories");}
  struct CloseDirectory {void operator()(DIR* value)const noexcept{if(value){::closedir(value);}}};
  std::unique_ptr<DIR,CloseDirectory> entries(::fdopendir(copy.release()));
  if(!entries){io("Cannot enumerate plugin data directories");}std::optional<std::string> selected;std::size_t count=0;
  errno=0;
  while(auto* entry=::readdir(entries.get())){
    const auto name=std::string(entry->d_name);if(name=="."||name==".."){continue;}
    if(++count>1024){fail(ErrorCode::InvalidFormat,"Too many plugin directory entries for bounded scheduler discovery");}
    if(!validLeaf(name)){fail(ErrorCode::UnsafePath,"Invalid plugin directory leaf");}
    struct stat st{};if(::fstatat(plugins,name.c_str(),&st,AT_SYMLINK_NOFOLLOW)!=0){io("Cannot inspect plugin data candidate");}
    if(!S_ISDIR(st.st_mode)||st.st_uid!=::geteuid()){continue;}
    auto dir=directory(plugins,name.c_str());const auto properties=read(dir.get(),"plugin.properties",65536);
    bool match=properties.bytes&&descriptorMatches(*properties.bytes);
    if(!properties.bytes&&hasPluginKey(config,name)){
      // Java's descriptor may be inside an opaque jar. Data+its exact config
      // namespace identify data only; no profile jar/native code is executed.
      const bool xml=regularEvidence(dir.get(),XmlName,XmlMaximum);
      const bool own=regularEvidence(dir.get(),OwnershipName,scheduler::OwnershipFile::MaximumBytes);match=xml||own;
    }
    if(name=="SpeedScheduler"&&!properties.bytes){const auto marker=read(dir.get(),MarkerName,128);if(marker.bytes&&*marker.bytes==Marker){match=true;}}
    if(match){if(selected){fail(ErrorCode::Conflict,"Multiple SpeedScheduler data directories are ambiguous");}selected=name;}
    errno=0;
  }
  if(errno){io("Cannot finish scheduler directory discovery");}
  if(selected){return *selected;}
  struct stat st{};
  if(::fstatat(plugins,"SpeedScheduler",&st,AT_SYMLINK_NOFOLLOW)==0){fail(ErrorCode::Conflict,"Existing SpeedScheduler directory lacks a unique data/config identity");}
  if(errno!=ENOENT){io("Cannot inspect default scheduler directory");}return "SpeedScheduler";
}
std::int32_t sourceInt(std::int64_t value) noexcept {return std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(value));}
SchedulerSettings settings(const core::Document& document,std::string_view leaf,std::string& diagnostic) {
  const auto prefix="Plugin."+std::string(leaf)+".";
  const auto integer=[&](const char* name,int defaultValue){
    const auto key=prefix+name;std::size_t count=0;
    for(std::size_t i=0;i<document.root().children.size();i+=2){if(document.root().children[i].bytes==key){++count;}}
    if(count>1){fail(ErrorCode::InvalidFormat,"Duplicate scheduler configuration key");}
    const auto* node=document.find(key);if(!node){return defaultValue;}
    if(node->type!=core::Node::Type::Integer){diagnostic="Malformed scheduler settings use original defaults; untouched bytes are preserved.";return defaultValue;}
    return static_cast<int>(sourceInt(node->integer));
  };
  return {integer("speedscheduler.enabled",0)!=0,integer("use.tags.not.cats",0)!=0,integer("override.forced",0)!=0,
    std::max(1000,integer("thread.sleep.time",10000))};
}
void safeConfiguration(const scheduler::Configuration& config) {
  const auto safe=[](int value){return value>=0&&value<=INT_MAX/1024;};
  if(!safe(config.defaultUpload)||!safe(config.defaultDownload)){fail(ErrorCode::Unsupported,"Scheduler rates exceed native byte-rate representation");}
  for(const auto& item:config.schedules){if(!safe(item.upload)||!safe(item.download)){fail(ErrorCode::Unsupported,"Scheduler rate exceeds native byte-rate representation");}}
}
std::string intentText(const SchedulerAction& action) {
  return "BiglyBTC Scheduler intent 1\n"+action.hash.hex()+"\n"+std::to_string(action.token)+"\n"+std::to_string(action.manualRevision)+"\n"+std::to_string(static_cast<int>(action.kind))+"\n";
}
bool intentFor(std::string_view bytes,const core::TorrentHash& hash) {
  return bytes.starts_with("BiglyBTC Scheduler intent 1\n"+hash.hex()+"\n");
}
}
struct SchedulerService::Impl {
  plugins::Module module;
  struct stat profile{};
  detail::Descriptor plugins{-1},data{-1};
  SchedulerSnapshot snapshot;
  Raw xml,ownership,intent,config;
  std::map<std::string,std::pair<std::uint64_t,std::uint64_t>> authority;
  std::optional<SchedulerAction> pending;
  std::optional<SchedulerPlan> issued;
  std::map<std::uint64_t,SchedulerTorrent> planInputs;
  std::set<std::uint64_t> consumed;
  std::uint64_t planSequence=0;
  bool nonDefaultUpload=true,nonDefaultDownload=true,restoreDefaults=false,poisoned=false,inheritedRecords=false;
  explicit Impl(plugins::Module&& value):module(std::move(value)){}
  void held(const ProfileLease& lease) const {
    if(!lease.valid()){fail(ErrorCode::Busy,"Scheduler requires a held profile lease");}struct stat now{};
    if(::fstat(lease.directoryHandle(),&now)!=0){io("Cannot inspect scheduler profile lease");}
    if(now.st_dev!=profile.st_dev||now.st_ino!=profile.st_ino||!S_ISDIR(now.st_mode)||!now.st_nlink){fail(ErrorCode::Conflict,"Scheduler was used with another/replaced profile");}
    if(plugins.get()>=0){directoryIdentity(lease.directoryHandle(),"plugins",plugins.get());}
    if(data.get()>=0){directoryIdentity(plugins.get(),snapshot.directoryLeaf.c_str(),data.get());}
  }
  void makeData(const ProfileLease& lease) {
    held(lease);
    if(plugins.get()<0){
      if(::mkdirat(lease.directoryHandle(),"plugins",0700)!=0){if(errno==EEXIST){fail(ErrorCode::Conflict,"Plugin directory appeared after scheduler read");}io("Cannot create owned plugin data root");}
      if(::fsync(lease.directoryHandle())!=0){io("Cannot sync new plugin data root");}plugins=directory(lease.directoryHandle(),"plugins");
    }
    if(data.get()<0){
      if(::mkdirat(plugins.get(),snapshot.directoryLeaf.c_str(),0700)!=0){if(errno==EEXIST){fail(ErrorCode::Conflict,"Scheduler directory appeared after read");}io("Cannot create owned scheduler data directory");}
      if(::fsync(plugins.get())!=0){io("Cannot sync scheduler data directory");}data=directory(plugins.get(),snapshot.directoryLeaf.c_str());
      (void)replace(data.get(),MarkerName,{},Marker,128);
    }
    held(lease);
  }
  void invalidatePlans(){issued.reset();planInputs.clear();consumed.clear();}
  void warning(){
    snapshot.interruptedAction=intent.bytes.has_value()||poisoned;
    snapshot.inheritedOwnershipUnverified=inheritedRecords;
    if(snapshot.interruptedAction){snapshot.diagnostic="An interrupted scheduler action requires review. Automatic actions are suspended.";}
    else if(snapshot.inheritedOwnershipUnverified&&snapshot.diagnostic.empty()){
      snapshot.diagnostic="Existing pause records are preserved. Only pauses acknowledged in this session may be resumed automatically.";}
  }
  void advance(){if(snapshot.revision==UINT64_MAX){fail(ErrorCode::Conflict,"Scheduler revision exhausted");}++snapshot.revision;invalidatePlans();}
};
SchedulerService::SchedulerService(std::unique_ptr<Impl> value):impl_(std::move(value)){}
SchedulerService::~SchedulerService()=default;
const SchedulerSnapshot& SchedulerService::snapshot()const noexcept{return impl_->snapshot;}
std::expected<std::unique_ptr<SchedulerService>,Error> SchedulerService::open(const ProfileLease& lease,plugins::Module&& module,std::stop_token stop) {
  return guarded([&]()->std::expected<std::unique_ptr<SchedulerService>,Error>{
    checkStop(stop);if(!lease.valid()){fail(ErrorCode::Busy,"Scheduler requires held profile lease");}
    if(module.id()!="SpeedScheduler"){fail(ErrorCode::Unsupported,"Wrong module supplied to scheduler service");}
    auto impl=std::make_unique<Impl>(std::move(module));
    if(::fstat(lease.directoryHandle(),&impl->profile)!=0){io("Cannot inspect scheduler lease");}
    auto service=std::unique_ptr<SchedulerService>(new SchedulerService(std::move(impl)));
    auto loaded=service->reload(lease,stop);if(!loaded){return std::unexpected(loaded.error());}return service;
  });
}
std::expected<void,Error> SchedulerService::reload(const ProfileLease& lease,std::stop_token stop) {
  return guarded([&]()->std::expected<void,Error>{
    auto& p=*impl_;p.held(lease);checkStop(stop);
    if(p.pending){fail(ErrorCode::Busy,"Cannot reload while scheduler action is awaiting acknowledgement");}
    auto cfg=configuration(lease);auto plugins=directory(lease.directoryHandle(),"plugins",true);
    const auto leaf=locate(plugins.get(),cfg.document);auto data=plugins.get()<0?detail::Descriptor(-1):directory(plugins.get(),leaf.c_str(),true);
    auto xml=data.get()<0?Raw{}:read(data.get(),XmlName,XmlMaximum);
    auto own=data.get()<0?Raw{}:read(data.get(),OwnershipName,scheduler::OwnershipFile::MaximumBytes);
    auto intent=data.get()<0?Raw{}:read(data.get(),IntentName,IntentMaximum);
    std::optional<scheduler::Configuration> model;
    if(xml.bytes){auto parsed=scheduler::readOriginalXml(*xml.bytes);if(!parsed){fail(ErrorCode::InvalidFormat,"Invalid original SavedSchedules.xml; no normalization attempted");}model=std::move(parsed->configuration);}
    if(own.bytes&&!scheduler::OwnershipFile::read(*own.bytes)){fail(ErrorCode::InvalidFormat,"Invalid bounded scheduler ownership file");}
    std::string diagnostic;const auto values=settings(cfg.document,leaf,diagnostic);checkStop(stop);
    if(!p.snapshot.revision||leaf!=p.snapshot.directoryLeaf||own.bytes!=p.ownership.bytes){p.authority.clear();p.inheritedRecords=own.bytes&&!own.bytes->empty();}
    p.plugins=std::move(plugins);p.data=std::move(data);p.xml=std::move(xml);p.ownership=std::move(own);p.intent=std::move(intent);p.config=std::move(cfg.raw);
    p.snapshot.directoryLeaf=leaf;p.snapshot.settings=values;p.snapshot.configuration=std::move(model);
    p.snapshot.originalXml=p.xml.bytes.value_or("");p.snapshot.ownershipBytes=p.ownership.bytes.value_or("");p.snapshot.diagnostic=std::move(diagnostic);
    p.advance();p.warning();return {};
  });
}
std::expected<void,Error> SchedulerService::saveSchedules(const ProfileLease& lease,std::uint64_t revision,const SchedulerXmlChanges& changes,std::stop_token stop) {
  return guarded([&]()->std::expected<void,Error>{
    auto& p=*impl_;p.held(lease);checkStop(stop);
    if(revision!=p.snapshot.revision){fail(ErrorCode::Conflict,"Scheduler editor revision is stale");}
    if(p.pending||p.poisoned){fail(ErrorCode::Busy,"Scheduler action must settle before editing");}
    auto edited=patchSchedulerXml(p.snapshot.originalXml,changes);if(!edited){return std::unexpected(edited.error());}
    auto parsed=scheduler::readOriginalXml(*edited);if(!parsed){fail(ErrorCode::InvalidFormat,"Edited scheduler model failed validation");}
    safeConfiguration(parsed->configuration);checkStop(stop);p.makeData(lease);
    try{
      p.xml=replace(p.data.get(),XmlName,p.xml,*edited,XmlMaximum);
      p.snapshot.originalXml=*edited;p.snapshot.configuration=std::move(parsed->configuration);p.advance();
    }catch(...){p.poisoned=true;p.warning();throw;}return {};
  });
}
std::expected<void,Error> SchedulerService::saveSettings(const ProfileLease& lease,std::uint64_t revision,const SchedulerSettingsChanges& changes,std::stop_token stop) {
  return guarded([&]()->std::expected<void,Error>{
    auto& p=*impl_;p.held(lease);checkStop(stop);if(revision!=p.snapshot.revision){fail(ErrorCode::Conflict,"Scheduler settings revision is stale");}
    if(p.pending||p.poisoned){fail(ErrorCode::Busy,"Scheduler action must settle before editing settings");}
    if(changes.pollMilliseconds&&*changes.pollMilliseconds<1000){fail(ErrorCode::InvalidFormat,"Scheduler interval must be at least 1000 milliseconds");}
    auto cfg=configuration(lease);if(cfg.raw.bytes!=p.config.bytes||(cfg.raw.bytes&&!same(cfg.raw.identity,p.config.identity))){fail(ErrorCode::Conflict,"Configuration changed before scheduler settings save");}
    const auto prefix="Plugin."+p.snapshot.directoryLeaf+".";
    const auto put=[&](const char* key,std::optional<std::int64_t> value){if(value&&!cfg.document.replaceValue(prefix+key,"i"+std::to_string(*value)+"e")){fail(ErrorCode::InvalidFormat,"Cannot patch scheduler setting");}};
    put("speedscheduler.enabled",changes.enabled?std::optional<std::int64_t>(*changes.enabled):std::nullopt);
    put("use.tags.not.cats",changes.useTags?std::optional<std::int64_t>(*changes.useTags):std::nullopt);
    put("override.forced",changes.overrideForced?std::optional<std::int64_t>(*changes.overrideForced):std::nullopt);
    put("thread.sleep.time",changes.pollMilliseconds?std::optional<std::int64_t>(*changes.pollMilliseconds):std::nullopt);
    std::string newDiagnostic;const auto newSettings=settings(cfg.document,p.snapshot.directoryLeaf,newDiagnostic);
    const auto bytes=cfg.document.serialize();checkStop(stop);unchanged(lease.directoryHandle(),"biglybt.config",p.config,ConfigMaximum);
    if(bytes==cfg.document.originalBytes()){if(changes.enabled&&!*changes.enabled){p.restoreDefaults=true;p.invalidatePlans();}return {};}
    try{
    if(p.config.bytes){
      auto file=ConfigFile::load(lease,"biglybt.config");if(!file){throw Failure{file.error()};}
      if(file->recoverySource()!=RecoverySource::Primary||file->document().originalBytes()!=*p.config.bytes){fail(ErrorCode::Conflict,"Configuration changed while preparing scheduler save");}
      file->document()=std::move(cfg.document);auto saved=file->save(lease);if(!saved){p.poisoned=true;p.warning();throw Failure{saved.error()};}
    }else{auto saved=ConfigFile::createMissing(lease,"biglybt.config",bytes);if(!saved){p.poisoned=true;p.warning();throw Failure{saved.error()};}}
    p.config=read(lease.directoryHandle(),"biglybt.config",ConfigMaximum);
    const bool wasEnabled=p.snapshot.settings.enabled;p.snapshot.settings=newSettings;
    if((wasEnabled||changes.enabled.has_value())&&!p.snapshot.settings.enabled){p.restoreDefaults=true;}
    p.snapshot.diagnostic=std::move(newDiagnostic);p.advance();p.warning();
    }catch(...){p.poisoned=true;p.warning();throw;}return {};
  });
}
std::expected<SchedulerPlan,Error> SchedulerService::evaluate(scheduler::LocalMinute minute,std::span<const SchedulerTorrent> torrents,int upload,int download) {
  return guarded([&]()->std::expected<SchedulerPlan,Error>{
    auto& p=*impl_;
    if(p.pending||p.snapshot.interruptedAction){fail(ErrorCode::Busy,"Interrupted/unacknowledged scheduler action blocks automatic effects");}
    if(minute.sundayBasedDay<0||minute.sundayBasedDay>6||minute.minute<0||minute.minute>1439||upload<0||download<0
        ||upload>INT_MAX/1024||download>INT_MAX/1024||torrents.size()>scheduler::MaximumTorrents){fail(ErrorCode::InvalidFormat,"Invalid scheduler tick input");}
    if(p.snapshot.settings.enabled&&!p.snapshot.configuration){fail(ErrorCode::Missing,"No SavedSchedules.xml; scheduler has no schedule to enforce");}
    if(p.snapshot.configuration){safeConfiguration(*p.snapshot.configuration);}
    if(p.planSequence==UINT64_MAX){fail(ErrorCode::Conflict,"Scheduler plan sequence exhausted");}
    std::vector<bt_scheduler_torrent_v1> input;input.reserve(torrents.size());
    std::vector<std::vector<const char*>> memberships(torrents.size());
    std::map<std::uint64_t,const SchedulerTorrent*> byToken;std::set<std::string> hashes;std::size_t membershipCount=0;
    auto owned=scheduler::OwnershipFile::read(p.snapshot.ownershipBytes);if(!owned){fail(ErrorCode::InvalidFormat,"Invalid scheduler ownership source");}
    const bool filters=p.snapshot.configuration&&std::ranges::any_of(p.snapshot.configuration->schedules,[](const auto& rule){return rule.enabled&&(rule.onlyCategory||rule.excludeCategory);});
    for(std::size_t i=0;i<torrents.size();++i){
      const auto& item=torrents[i];const auto state=static_cast<int>(item.state);
      if(!item.token||state<1||state>9||(item.hash.bytes().size()!=20&&item.hash.bytes().size()!=32)||!byToken.emplace(item.token,&item).second||!hashes.insert(item.hash.hex()).second){
        fail(ErrorCode::InvalidFormat,"Scheduler torrent identities/states must be valid and unique");}
      if(filters&&!item.membershipsAvailable){fail(ErrorCode::Unsupported,"Filtered schedules require actual category/tag membership");}
      if(item.memberships.size()>1024||item.memberships.size()>1000000-membershipCount){fail(ErrorCode::InvalidFormat,"Too many scheduler memberships");}membershipCount+=item.memberships.size();
      for(const auto& name:item.memberships){if(name.size()>4096||name.find('\0')!=name.npos){fail(ErrorCode::InvalidFormat,"Invalid scheduler membership text");}memberships[i].push_back(name.c_str());}
      const auto authority=p.authority.find(item.hash.hex());const auto present=owned->contains(hashBytes(item.hash));if(!present){fail(ErrorCode::InvalidFormat,"Invalid scheduler ownership hash");}
      const bool ours=*present&&authority!=p.authority.end()&&authority->second==std::pair{item.token,item.manualRevision};
      const auto flags=(item.complete?BT_TORRENT_COMPLETE:0U)|(item.forceStarted?BT_TORRENT_FORCED:0U)|(item.paused?BT_TORRENT_PAUSED:0U)|(ours?BT_TORRENT_OWNED_PAUSE:0U);
      input.push_back({item.token,static_cast<std::uint32_t>(state),flags,memberships[i].data(),static_cast<std::uint32_t>(memberships[i].size()),0});
    }
    const std::string empty="<SpeedScheduler><schedules/></SpeedScheduler>";
    const auto& xml=p.xml.bytes?*p.xml.bytes:empty;
    bt_scheduler_request_v1 request{reinterpret_cast<const std::uint8_t*>(xml.data()),static_cast<std::uint32_t>(xml.size()),
      static_cast<std::uint32_t>(minute.sundayBasedDay),static_cast<std::uint32_t>(minute.minute),
      (p.snapshot.settings.enabled?BT_SCHEDULER_ENABLED:0U)|(p.snapshot.settings.overrideForced?BT_SCHEDULER_OVERRIDE_FORCED:0U)
        |(p.nonDefaultUpload?BT_SCHEDULER_NONDEFAULT_UPLOAD:0U)|(p.nonDefaultDownload?BT_SCHEDULER_NONDEFAULT_DOWNLOAD:0U),
      upload,download,input.data(),static_cast<std::uint32_t>(input.size()),0};
    // Original policy can return Pause followed by SetStopReason for each item.
    std::vector<bt_scheduler_action_v1> actions(torrents.size()*2);
    bt_scheduler_result_v1 response{actions.data(),static_cast<std::uint32_t>(actions.size()),0,0,0,0,0};
    if(p.module.call(BT_SCHEDULER_EVALUATE,&request,sizeof(request),&response,sizeof(response))!=BT_OK){fail(ErrorCode::Unsupported,"Approved scheduler module rejected tick");}
    constexpr std::uint32_t Allowed=BT_SCHEDULER_NONDEFAULT_UPLOAD|BT_SCHEDULER_NONDEFAULT_DOWNLOAD|BT_SCHEDULER_UPLOAD_CHANGED|BT_SCHEDULER_DOWNLOAD_CHANGED;
    if(response.reserved||(response.flags&~Allowed)||response.action_count>actions.size()||response.effective_upload<0||response.global_download<0
        ||response.effective_upload>INT_MAX/1024||response.global_download>INT_MAX/1024){fail(ErrorCode::InvalidFormat,"Scheduler module returned invalid bounded result");}
    SchedulerPlan plan;plan.id=++p.planSequence;plan.revision=p.snapshot.revision;
    if(response.flags&BT_SCHEDULER_UPLOAD_CHANGED){plan.uploadKiB=response.effective_upload;}
    if(response.flags&BT_SCHEDULER_DOWNLOAD_CHANGED){plan.downloadKiB=response.global_download;}
    plan.nonDefaultUpload=(response.flags&BT_SCHEDULER_NONDEFAULT_UPLOAD)!=0;plan.nonDefaultDownload=(response.flags&BT_SCHEDULER_NONDEFAULT_DOWNLOAD)!=0;
    if(p.restoreDefaults&&p.snapshot.configuration){plan.uploadKiB=p.snapshot.configuration->defaultUpload;plan.downloadKiB=p.snapshot.configuration->defaultDownload;plan.nonDefaultUpload=false;plan.nonDefaultDownload=false;}
    for(std::size_t i=0;i<response.action_count;++i){
      const auto& action=actions[i];const auto item=byToken.find(action.token);
      if(action.reserved||action.kind<BT_SCHEDULER_PAUSE||action.kind>BT_SCHEDULER_STOP_REASON||item==byToken.end()){fail(ErrorCode::InvalidFormat,"Scheduler module returned invalid action identity");}
      plan.actions.push_back({plan.id,static_cast<std::uint64_t>(i),action.token,item->second->manualRevision,item->second->hash,static_cast<scheduler::ActionKind>(action.kind-1)});
    }
    std::map<std::uint64_t,SchedulerTorrent> planned;for(const auto& item:torrents){planned.emplace(item.token,item);}
    p.planInputs=std::move(planned);p.issued=plan;p.consumed.clear();return plan;
  });
}
std::expected<void,Error> SchedulerService::beginAction(const ProfileLease& lease,const SchedulerAction& action,const SchedulerTorrent& current,std::stop_token stop) {
  return guarded([&]()->std::expected<void,Error>{
    auto& p=*impl_;p.held(lease);checkStop(stop);
    if(p.pending||p.snapshot.interruptedAction){fail(ErrorCode::Busy,"A scheduler action is already pending/interrupted");}
    if(!p.issued||action.plan!=p.issued->id||p.issued->revision!=p.snapshot.revision||action.ordinal>=p.issued->actions.size()
        ||p.issued->actions[action.ordinal]!=action||p.consumed.contains(action.ordinal)||current.hash!=action.hash||current.token!=action.token||current.manualRevision!=action.manualRevision){
      fail(ErrorCode::Conflict,"Scheduler action is stale or not the exact issued action");}
    const auto before=p.planInputs.find(current.token);
    if(before==p.planInputs.end()||before->second.complete!=current.complete||before->second.forceStarted!=current.forceStarted
        ||before->second.paused!=current.paused||before->second.state!=current.state||before->second.membershipsAvailable!=current.membershipsAvailable
        ||before->second.memberships!=current.memberships){fail(ErrorCode::Conflict,"Scheduler torrent state changed after evaluation");}
    if(action.kind==scheduler::ActionKind::Resume||action.kind==scheduler::ActionKind::Restart){
      const auto found=p.authority.find(action.hash.hex());if(found==p.authority.end()||found->second!=std::pair{action.token,action.manualRevision}){fail(ErrorCode::Conflict,"Scheduler lacks acknowledged current-session pause ownership");}}
    unchanged(lease.directoryHandle(),"biglybt.config",p.config,ConfigMaximum);
    if(p.data.get()>=0){unchanged(p.data.get(),XmlName,p.xml,XmlMaximum);unchanged(p.data.get(),OwnershipName,p.ownership,scheduler::OwnershipFile::MaximumBytes);}
    checkStop(stop);p.makeData(lease);
    try{p.intent=replace(p.data.get(),IntentName,p.intent,intentText(action),IntentMaximum);}
    catch(...){p.poisoned=true;p.warning();throw;}
    p.pending=action;p.warning();return {};
  });
}
std::expected<void,Error> SchedulerService::finishAction(const ProfileLease& lease,const SchedulerAction& action,SchedulerActionResult result) {
  return guarded([&]()->std::expected<void,Error>{
    auto& p=*impl_;p.held(lease);
    if(!p.pending||*p.pending!=action||!p.intent.bytes||*p.intent.bytes!=intentText(action)){fail(ErrorCode::Conflict,"Scheduler acknowledgement does not match durable pending action");}
    if(result==SchedulerActionResult::Indeterminate){p.poisoned=true;p.warning();fail(ErrorCode::Conflict,"Scheduler action outcome is uncertain; no automatic retry");}
    if(result!=SchedulerActionResult::Succeeded&&result!=SchedulerActionResult::FailedBeforeEffect){fail(ErrorCode::InvalidFormat,"Invalid scheduler action acknowledgement");}
    try{
      if(result==SchedulerActionResult::Succeeded&&(action.kind==scheduler::ActionKind::Pause||action.kind==scheduler::ActionKind::Resume||action.kind==scheduler::ActionKind::Restart)){
        auto file=scheduler::OwnershipFile::read(p.snapshot.ownershipBytes);if(!file){fail(ErrorCode::InvalidFormat,"Invalid scheduler pause source");}
        auto bytes=action.kind==scheduler::ActionKind::Pause?file->afterPause(hashBytes(action.hash)):file->afterResume(hashBytes(action.hash));
        if(!bytes){fail(ErrorCode::InvalidFormat,"Cannot edit original scheduler ownership without erasing unknown bytes");}
        if(*bytes!=p.snapshot.ownershipBytes){p.ownership=replace(p.data.get(),OwnershipName,p.ownership,*bytes,scheduler::OwnershipFile::MaximumBytes);p.snapshot.ownershipBytes=*bytes;}
      }
      remove(p.data.get(),IntentName,p.intent,IntentMaximum);p.intent={};
    }catch(...){p.authority.erase(action.hash.hex());p.poisoned=true;p.warning();throw;}
    if(result==SchedulerActionResult::Succeeded&&action.kind==scheduler::ActionKind::Pause){p.authority[action.hash.hex()]={action.token,action.manualRevision};}
    else if(result==SchedulerActionResult::Succeeded&&(action.kind==scheduler::ActionKind::Resume||action.kind==scheduler::ActionKind::Restart)){p.authority.erase(action.hash.hex());}
    if(p.snapshot.ownershipBytes.empty()){p.inheritedRecords=false;}
    p.consumed.insert(action.ordinal);p.pending.reset();p.snapshot.diagnostic.clear();p.warning();return {};
  });
}
std::expected<void,Error> SchedulerService::acknowledgeRates(std::uint64_t plan,bool uploadApplied,bool downloadApplied) {
  return guarded([&]()->std::expected<void,Error>{
    auto& p=*impl_;if(!p.issued||p.issued->id!=plan||p.issued->revision!=p.snapshot.revision){fail(ErrorCode::Conflict,"Scheduler rate acknowledgement is stale");}
    if(!p.issued->uploadKiB||uploadApplied){p.nonDefaultUpload=p.issued->nonDefaultUpload;}
    if(!p.issued->downloadKiB||downloadApplied){p.nonDefaultDownload=p.issued->nonDefaultDownload;}
    if((!p.issued->uploadKiB||uploadApplied)&&(!p.issued->downloadKiB||downloadApplied)){p.restoreDefaults=false;}return {};
  });
}
std::expected<void,Error> SchedulerService::revokeForManual(const ProfileLease& lease,const core::TorrentHash& hash) {
  // No filesystem failure can restore in-memory authority over a user's action.
  auto& p=*impl_;p.authority.erase(hash.hex());p.invalidatePlans();
  return guarded([&]()->std::expected<void,Error>{
    p.held(lease);if(hash.bytes().size()!=20&&hash.bytes().size()!=32){fail(ErrorCode::InvalidFormat,"Invalid manual scheduler revocation hash");}
    if(p.data.get()<0){return {};}
    auto file=scheduler::OwnershipFile::read(p.snapshot.ownershipBytes);if(!file){fail(ErrorCode::InvalidFormat,"Invalid scheduler ownership file");}
    auto canonical=scheduler::originalOwnershipHash(hashBytes(hash));if(!canonical){fail(ErrorCode::InvalidFormat,"Invalid manual revocation hash");}
    // A manual command revokes all duplicate legacy grants in one bounded pass.
    // Ordinary scheduler Resume still uses source-compatible first-only removal.
    std::string bytes;bytes.reserve(p.snapshot.ownershipBytes.size());const auto& source=p.snapshot.ownershipBytes;
    for(std::size_t pos=0;pos<source.size();){
      const auto separator=source.find_first_of("\r\n",pos);const auto end=separator==source.npos?source.size():separator;
      auto after=end;if(after<source.size()){++after;if(source[end]=='\r'&&after<source.size()&&source[after]=='\n'){++after;}}
      auto first=pos,last=end;while(first<last&&static_cast<unsigned char>(source[first])<=32){++first;}
      while(last>first&&static_cast<unsigned char>(source[last-1])<=32){--last;}
      if(std::string_view(source).substr(first,last-first)!=*canonical){bytes.append(source,pos,after-pos);}pos=after;
    }
    if(bytes!=p.snapshot.ownershipBytes){p.ownership=replace(p.data.get(),OwnershipName,p.ownership,bytes,scheduler::OwnershipFile::MaximumBytes);p.snapshot.ownershipBytes=std::move(bytes);}
    if(p.intent.bytes&&intentFor(*p.intent.bytes,hash)){
      remove(p.data.get(),IntentName,p.intent,IntentMaximum);p.intent={};p.pending.reset();
      // Explicit user override settles this intent only. A previous unrelated IO
      // uncertainty still requires a new service/review rather than blind retry.
    }
    if(p.snapshot.ownershipBytes.empty()){p.inheritedRecords=false;}
    p.snapshot.diagnostic.clear();p.warning();return {};
  });
}
std::expected<bool,Error> SchedulerService::rebindOwnedPause(const ProfileLease& lease,
    const core::TorrentHash& hash,std::uint64_t oldToken,std::uint64_t newToken,std::uint64_t manualRevision) {
  return guarded([&]()->std::expected<bool,Error>{
    auto& p=*impl_;p.held(lease);
    if((hash.bytes().size()!=20&&hash.bytes().size()!=32)||!oldToken||newToken<=oldToken){
      fail(ErrorCode::InvalidFormat,"Invalid scheduler pause admission transfer");}
    if(p.pending||p.intent.bytes||p.poisoned||p.snapshot.interruptedAction){
      fail(ErrorCode::Busy,"An unsettled scheduler action prevents pause admission transfer");}
    unchanged(lease.directoryHandle(),"biglybt.config",p.config,ConfigMaximum);
    if(p.data.get()>=0){
      unchanged(p.data.get(),XmlName,p.xml,XmlMaximum);
      unchanged(p.data.get(),OwnershipName,p.ownership,scheduler::OwnershipFile::MaximumBytes);
      unchanged(p.data.get(),IntentName,p.intent,IntentMaximum);
    }
    const auto found=p.authority.find(hash.hex());
    if(found==p.authority.end()){return false;}
    if(found->second!=std::pair{oldToken,manualRevision}){
      fail(ErrorCode::Conflict,"Scheduler pause authority does not match the retired admission");}
    const auto owned=scheduler::OwnershipFile::read(p.snapshot.ownershipBytes);
    if(!owned){fail(ErrorCode::Conflict,"Acknowledged scheduler pause source is invalid");}
    const auto present=owned->contains(hashBytes(hash));
    if(!present||!*present){fail(ErrorCode::Conflict,"Acknowledged scheduler pause record is unavailable");}
    // advance() checks exhaustion before mutating anything and invalidates both
    // issued actions and rate acknowledgements bound to the old admission.
    p.advance();found->second={newToken,manualRevision};return true;
  });
}
} // namespace biglybtc::storage
