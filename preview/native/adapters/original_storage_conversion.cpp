#include "original_storage_conversion.hpp"
#include <algorithm>
#include <fcntl.h>
#include <limits>
#include <set>
#include <sys/stat.h>
#include <unistd.h>

namespace biglybtc::storage {
namespace {
using Node=core::Node;using Kind=OriginalStorageKind;using Stage=OriginalStorageConversionStage;
using Observer=void (*)(Stage);
constexpr std::string_view IntentKey="BiglyBTC.compactConversion";
struct Failure {Error error;};
void require(bool condition,ErrorCode code,const char* message){if(!condition){throw Failure{{code,message}};}}
template<class T>T take(std::expected<T,Error> value){if(!value){throw Failure{value.error()};}return std::move(*value);}
void done(std::expected<void,Error> value){if(!value){throw Failure{value.error()};}}
template<class F>auto guarded(F&& call)->decltype(call()){
  try{return call();}catch(const Failure& error){return std::unexpected(error.error);}
  catch(const std::bad_alloc&){return std::unexpected(Error{ErrorCode::Io,"Original conversion allocation failed"});}
  catch(const std::exception&){return std::unexpected(Error{ErrorCode::Io,"Original conversion interrupted"});}
}
std::string encoded(const Node& node){auto value=core::encodeValue(node,compatibleConfigLimits());require(value.has_value(),ErrorCode::InvalidFormat,"Original conversion value exceeds limits");return std::move(*value);}
core::Document attributes(const TorrentStateFile& state){auto value=core::Document::parse(state.document().find("attributes")?encoded(*state.document().find("attributes")):"de",compatibleConfigLimits());require(value&&value->root().type==Node::Type::Dictionary,ErrorCode::InvalidFormat,"Invalid original conversion attributes");return std::move(*value);}
void put(core::Document& value,std::string_view key,std::string encodedValue){require(value.replaceValue(key,std::move(encodedValue)).has_value(),ErrorCode::InvalidFormat,"Original conversion edit exceeds limits");}
std::int64_t integer(const Node* node){require(node&&node->type==Node::Type::Integer,ErrorCode::InvalidFormat,"Invalid original conversion integer");return node->integer;}
std::string number(std::uint64_t value){return "i"+std::to_string(value)+'e';}
void observe(Observer call,Stage stage){if(call){call(stage);}}
void ready(const TorrentStateFile& state){
  require(state.prepared(),ErrorCode::Conflict,"Original conversion requires prepared state ownership");
  const auto* outer=state.document().find("resume");const auto* data=outer?outer->find("data"):nullptr;
  const auto* valid=data?data->find("valid"):nullptr;const auto* bits=data?data->find("resume data"):nullptr;const auto* blocks=data?data->find("blocks"):nullptr;
  require(valid&&valid->type==Node::Type::Integer&&valid->integer==0&&bits&&bits->type==Node::Type::Bytes
      &&std::ranges::all_of(bits->bytes,[](unsigned char value){return value<=3&&value!=1;})
      &&(!blocks||(blocks->type==Node::Type::Dictionary&&blocks->children.empty())),ErrorCode::Conflict,"Original conversion requires conservative unverified resume");
}
struct Snapshot {OriginalFileIoIdentity identity;OriginalFileGeometry geometry;std::size_t count;struct stat file;};
Snapshot authenticate(const ProfileLease& lease,const TorrentStateFile& state,std::size_t index,int fd,
    OriginalFileIo& io,const std::shared_ptr<OriginalControl>& control,Kind family){
  ready(state);const auto identity=take(io.payloadIdentity());struct stat file{};const auto flags=::fcntl(fd,F_GETFL);
  require(::fstat(fd,&file)==0&&S_ISREG(file.st_mode)&&file.st_uid==::geteuid()&&file.st_size>=0
      &&identity.device==static_cast<std::uint64_t>(file.st_dev)&&identity.inode==static_cast<std::uint64_t>(file.st_ino)
      &&identity.links==file.st_nlink&&identity.links>=1&&identity.links<=2&&flags>=0&&(flags&O_ACCMODE)==O_RDWR,
      ErrorCode::Conflict,"Original conversion descriptor differs from its authenticated IO");
  require(identity.device<=static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())&&identity.inode<=static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()),ErrorCode::Unsupported,"Original conversion identity exceeds journal range");
  require(control!=nullptr,ErrorCode::Conflict,"Original conversion requires its retained control owner");
  done(control->authenticate(lease,state.hash().hex(),index,family));(void)take(control->loadValidated(io.geometry()));
  auto attrs=attributes(state);const auto* types=attrs.find("storetypes");
  require(types&&types->type==Node::Type::List&&types->children.size()<=100000&&index<types->children.size(),ErrorCode::InvalidFormat,"Original conversion lacks complete storage types");
  for(const auto& type:types->children){require(type.type==Node::Type::Bytes&&(type.bytes=="L"||type.bytes=="C"||type.bytes=="R"||type.bytes=="X"),ErrorCode::InvalidFormat,"Unknown original conversion storage kind");}
  return {identity,io.geometry(),types->children.size(),file};
}
std::string pending(const core::Document& attrs,const Snapshot& snapshot,std::size_t index,bool include,bool& own){
  std::string result="l";std::set<std::size_t> seen;own=false;
  if(const auto* list=attrs.find(IntentKey)){
    require(list->type==Node::Type::List&&list->children.size()<=snapshot.count,ErrorCode::InvalidFormat,"Invalid original conversion intent list");
    for(const auto& row:list->children){
      const auto entry=integer(row.find("index")),device=integer(row.find("device")),inode=integer(row.find("inode"));
      require(row.type==Node::Type::Dictionary&&entry>=0&&static_cast<std::uint64_t>(entry)<snapshot.count&&device>=0&&inode>0&&seen.insert(static_cast<std::size_t>(entry)).second,ErrorCode::InvalidFormat,"Invalid original conversion intent identity");
      if(static_cast<std::size_t>(entry)==index){require(static_cast<std::uint64_t>(device)==snapshot.identity.device&&static_cast<std::uint64_t>(inode)==snapshot.identity.inode,ErrorCode::Conflict,"Original conversion intent payload changed");own=true;}
      else{result+=encoded(row);}
    }
  }
  if(include){result+="d5:index"+number(index)+"6:device"+number(snapshot.identity.device)+"5:inode"+number(snapshot.identity.inode)+'e';}
  return result+'e';
}
void save(const ProfileLease& lease,const TorrentStateStore& store,TorrentStateFile& state,core::Document attrs){done(state.replaceSection("attributes",attrs.serialize()));done(store.save(lease,state));}
void publishKind(const ProfileLease& lease,const TorrentStateStore& store,TorrentStateFile& state,std::size_t index,std::string_view target){
  auto attrs=attributes(state);const auto* types=attrs.find("storetypes");std::string next="l";
  for(std::size_t i=0;i<types->children.size();++i){next+=i==index?std::to_string(target.size())+':'+std::string(target):encoded(types->children[i]);}
  put(attrs,"storetypes",next+'e');save(lease,store,state,std::move(attrs));
}
}
std::expected<void,Error> expandOwnedOriginalCompact(const ProfileLease& lease,const TorrentStateStore& store,
    TorrentStateFile& state,std::size_t index,int fd,OriginalFileIo& io,const std::shared_ptr<OriginalControl>& control,Observer observer){
  return guarded([&]()->std::expected<void,Error>{
    const auto kind=io.kind();require(kind==Kind::Compact||kind==Kind::Linear,ErrorCode::Unsupported,"Compact expansion requires C or an exact L recovery");
    const auto snapshot=authenticate(lease,state,index,fd,io,control,Kind::Compact);auto attrs=attributes(state);bool own=false;
    const auto intents=pending(attrs,snapshot,index,true,own);const auto* types=attrs.find("storetypes");
    require(types->children[index].bytes=="C"||types->children[index].bytes=="L",ErrorCode::Conflict,"Compact conversion metadata differs from its original intent");
    // Existing Original requests can already say L while retained C controls
    // still describe packed data. Authenticate that exact control preimage.
    if(!own&&types->children[index].bytes=="L"){
      require(take(control->loadValidated(snapshot.geometry)).has_value(),ErrorCode::Conflict,"Requested linear data lacks its retained physical compact control");
    }
    require(kind==Kind::Compact||(own&&types->children[index].bytes=="L"&&static_cast<std::uint64_t>(snapshot.file.st_size)==snapshot.geometry.fileBytes),ErrorCode::Conflict,"Linear conversion recovery lacks its complete exact payload");
    put(attrs,"resumecomplete","i1e");put(attrs,IntentKey,intents);save(lease,store,state,std::move(attrs));observe(observer,Stage::IntentPublished);
    if(kind==Kind::Compact){done(io.convertToLinear());}observe(observer,Stage::PayloadSynced);
    publishKind(lease,store,state,index,"L");observe(observer,Stage::TypePublished);
    if(control){done(control->retire());}observe(observer,Stage::ControlRetired);
    attrs=attributes(state);bool accepted=false;put(attrs,IntentKey,pending(attrs,snapshot,index,false,accepted));require(accepted,ErrorCode::Conflict,"Original conversion intent disappeared");save(lease,store,state,std::move(attrs));
    io=take(OriginalFileIo::open(fd,snapshot.geometry,Kind::Linear,std::nullopt,{},false,false,snapshot.identity.links));observe(observer,Stage::Completed);return {};
  });
}
std::expected<void,Error> unskipOwnedOriginalReordered(const ProfileLease& lease,const TorrentStateStore& store,
    TorrentStateFile& state,std::size_t index,int fd,OriginalFileIo& io,const std::shared_ptr<OriginalControl>& control,Observer observer,bool preservePreviousControl){
  return guarded([&]()->std::expected<void,Error>{
    require(io.kind()==Kind::ReorderedCompact||io.kind()==Kind::Reordered,ErrorCode::Unsupported,"Reordered selection requires X/R IO");
    (void)authenticate(lease,state,index,fd,io,control,Kind::ReorderedCompact);
    auto attrs=attributes(state);const auto* types=attrs.find("storetypes");require(types->children[index].bytes=="X"||types->children[index].bytes=="R",ErrorCode::Conflict,"Reordered selection metadata differs from its IO");
    put(attrs,"resumecomplete","i1e");save(lease,store,state,std::move(attrs));observe(observer,Stage::ResumePublished);
    if(preservePreviousControl){
      auto reordered=take(OriginalFileIo::open(fd,io.geometry(),Kind::Reordered,io.controlBytes(),control->publisher(),false,false,take(io.payloadIdentity()).links));
      require(!reordered.recoveryPending(),ErrorCode::Conflict,"Reordered selection requires completion recovery first");io=std::move(reordered);
    }else{done(io.setReorderedKind(Kind::Reordered));}
    observe(observer,Stage::ControlPublished);
    publishKind(lease,store,state,index,"R");observe(observer,Stage::TypePublished);observe(observer,Stage::Completed);return {};
  });
}
}
