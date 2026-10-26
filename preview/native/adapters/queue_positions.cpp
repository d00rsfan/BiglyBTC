#include "queue_positions.hpp"
#include "application_settings.hpp"
#include "torrent_state.hpp"
#include "biglybtc/queue_positions.hpp"
#include <openssl/evp.h>
#include <algorithm>
#include <array>
#include <limits>
#include <map>
#include <new>
#include <set>
namespace biglybtc::storage {
namespace {
using Node=core::Node;
std::unexpected<Error> fail(ErrorCode code,const char* message){return std::unexpected(Error{code,message});}
std::string raw(const core::Document& document,const Node& value){return document.originalBytes().substr(value.begin,value.end-value.begin);}
std::string integer(std::uint32_t value){return "i"+std::to_string(value)+'e';}
std::expected<OriginalTorrentIdentity,Error> identity(const core::Document& source){
  const auto subject=originalTorrentInfoSubject(source);if(!subject){return std::unexpected(subject.error());}
  const auto* info=source.find("info");OriginalFullInfoHashes full;
  const auto digest=[&](const EVP_MD* algorithm,std::span<std::byte> output){
    unsigned size=0;std::array<unsigned char,EVP_MAX_MD_SIZE> value{};
    if(EVP_Digest(subject->data(),subject->size(),value.data(),&size,algorithm,nullptr)!=1||size!=output.size()){return false;}
    std::copy_n(reinterpret_cast<const std::byte*>(value.data()),output.size(),output.begin());return true;
  };
  if(info->find("pieces")){full.v1.emplace();if(!digest(EVP_sha1(),*full.v1)){return fail(ErrorCode::Io,"Cannot bind original queue v1 hash");}}
  if(info->find("file tree")){full.v2.emplace();if(!digest(EVP_sha256(),*full.v2)){return fail(ErrorCode::Io,"Cannot bind original queue v2 hash");}}
  return selectOriginalTorrentIdentity(source,{*subject,std::move(full)});
}
}
std::expected<std::string,Error> originalQueueMappingScope(const core::Document& source){
  try{
    const auto* attributes=source.find("attributes");
    if(attributes&&attributes->type!=Node::Type::Dictionary){return fail(ErrorCode::InvalidFormat,"Invalid original queue mapping attributes");}
    // Every attribute consumed by ManagedStorage::plan/payloadClaimPlan. The
    // fixed key order is independent of dictionary order and unrelated edits.
    constexpr std::array<std::string_view,8> keys{"filelinks2","storetypes","incompfilesuffix",
        "dnd_pfx","dnd_sf","dnd_al","BiglyBTC.completion","BiglyBTC.compactConversion"};
    std::string scope="d";
    if(attributes){for(const auto key:keys){if(const auto* value=attributes->find(key)){
      const auto encoded=core::encodeValue(*value,compatibleConfigLimits());
      if(!encoded){return fail(ErrorCode::InvalidFormat,"Invalid original queue mapping scope");}
      const auto prefix=std::to_string(key.size())+':'+std::string(key);
      if(prefix.size()+encoded->size()+1>compatibleConfigLimits().inputBytes-scope.size()){
        return fail(ErrorCode::InvalidFormat,"Original queue mapping exceeds limits");
      }
      scope+=prefix;scope+=*encoded;
    }}}
    scope+='e';return scope;
  }catch(const std::bad_alloc&){return fail(ErrorCode::Io,"Insufficient memory binding original queue mapping");}
}
std::expected<LibraryPositionNormalization,Error> normalizeLibraryPositions(const ProfileLease& lease,
    std::span<const LibraryPositionObservation> observations,std::span<const LibraryGroupTransition> transitions,bool newSeedsTop,bool* saveAttempted){
  if(saveAttempted){*saveAttempted=false;}
  try{
    auto file=ConfigFile::load(lease,"downloads.config");if(!file){return std::unexpected(file.error());}
    if(file->recoverySource()==RecoverySource::Saving){return fail(ErrorCode::Conflict,"Resolve inventory recovery before queue normalization");}
    const auto saved=core::readLibrary(file->document());if(!saved){return fail(ErrorCode::InvalidFormat,"Invalid queue normalization inventory");}
    for(const auto& issue:saved->issues){
      if(issue.pauseEntry||!saved->legacyMap){return fail(ErrorCode::InvalidFormat,"Malformed queue normalization row");}
      const auto& key=file->document().root().children.at(issue.sourceIndex*2);
      const auto& row=file->document().root().children.at(issue.sourceIndex*2+1);
      if(key.bytes!="pause_data"&&(row.find("torrent")||row.find("torrent_hash")||row.find("save_dir")||row.find("path"))){return fail(ErrorCode::InvalidFormat,"Malformed legacy queue row");}
    }
    if(saved->downloads.size()!=observations.size()||observations.size()>100000){return fail(ErrorCode::Unsupported,"Every original queue group needs a current owner observation");}
    std::map<std::string,const LibraryPositionObservation*> byHash;
    for(const auto& observation:observations){if(!byHash.emplace(observation.hash.hex(),&observation).second){return fail(ErrorCode::Conflict,"Duplicate queue completion observation");}}
    auto states=TorrentStateStore::open(lease);if(!states){return std::unexpected(states.error());}
    std::vector<core::QueuePositionInput> inputs;inputs.reserve(observations.size());
    std::map<std::string,std::uint64_t> tokens;
    for(const auto& row:saved->downloads){
      if(!row.hash||!row.persistent||!byHash.contains(row.hash->hex())||!tokens.emplace(row.hash->hex(),inputs.size()+1).second){return fail(ErrorCode::Unsupported,"Unproven or nonpersistent original completion group");}
      const auto& observation=*byHash.at(row.hash->hex());
      const auto& admitted=observation.admitted;
      if(admitted.hash!=row.hash||admitted.torrentFile!=row.torrentFile||admitted.saveDirectory!=row.saveDirectory
          ||admitted.saveFile!=row.saveFile||admitted.legacySavePath!=row.legacySavePath||!admitted.persistent){
        return fail(ErrorCode::Conflict,"Current saved mapping differs from its checked admission");
      }
      const auto& node=saved->legacyMap?file->document().root().children.at(row.sourceIndex*2+1):file->document().find("downloads")->children.at(row.sourceIndex);
      const auto policy=compat::originalFilePolicy(node,observation.policy.size());
      if(observation.policy.empty()||!policy||*policy!=observation.policy){return fail(ErrorCode::Conflict,"Current selected-file policy differs from its checked admission");}
      if(auto key=validateOriginalTorrentKey(observation.identity,*row.hash,true);!key){return std::unexpected(key.error());}
      const auto state=states->loadAttributes(lease,*row.hash);if(!state){return std::unexpected(state.error());}
      const auto mapping=originalQueueMappingScope(state->document());if(!mapping){return std::unexpected(mapping.error());}
      if(observation.mappingScope.empty()||*mapping!=observation.mappingScope){return fail(ErrorCode::Conflict,"Current physical mapping differs from its checked admission");}
      const auto actual=identity(state->document());if(!actual){return std::unexpected(actual.error());}
      const auto relation=compareOriginalTorrentIdentities(*actual,observation.identity);
      if(!relation){return std::unexpected(relation.error());}
      if(*relation!=OriginalIdentityRelation::SameSelected){return fail(ErrorCode::Conflict,"Current original queue raw identity changed");}
      inputs.push_back({static_cast<std::uint64_t>(inputs.size()+1),row.position,observation.complete,row.persistent});
    }
    const auto config=readApplicationSettings(lease);if(!config){return std::unexpected(config.error());}
    if(config->recoverySource==RecoverySource::Saving){return fail(ErrorCode::Conflict,"Resolve settings recovery before queue normalization");}
    const auto setting=[&](std::string_view key,std::int64_t fallback)->std::expected<std::int64_t,Error>{
      const auto* value=config->source?config->source->find(key):nullptr;
      if(!value){return fallback;}
      if(value->type!=Node::Type::Integer){return fail(ErrorCode::InvalidFormat,"Malformed original queue position setting");}
      return value->integer;
    };
    const auto autoReposition=setting("StartStopManager_bAutoReposition",0),rankMode=setting("StartStopManager_iRankType",1);
    if(!autoReposition){return std::unexpected(autoReposition.error());}if(!rankMode){return std::unexpected(rankMode.error());}
    if(static_cast<std::uint32_t>(*autoReposition)!=0&&static_cast<std::uint32_t>(*rankMode)!=0){
      return fail(ErrorCode::Unsupported,"Automatic ranked seed positions require complete original rank/provider authority");
    }
    std::vector<core::QueuePositionTransition> events;events.reserve(transitions.size());
    bool newDownloadsTop=false;
    if(std::ranges::any_of(transitions,[](const auto& event){return !event.complete;})){
      if(const auto* value=config->source?config->source->find("Add Torrent Queue Position"):nullptr){
        if(value->type!=Node::Type::Integer){return fail(ErrorCode::InvalidFormat,"Invalid Add Torrent Queue Position");}
        newDownloadsTop=static_cast<std::uint32_t>(value->integer)==0;
      }
    }
    for(const auto& event:transitions){
      const auto found=tokens.find(event.hash.hex());if(found==tokens.end()){return fail(ErrorCode::Conflict,"Queue transition target changed");}
      events.push_back({found->second,event.complete,event.complete?newSeedsTop:newDownloadsTop});
    }
    const auto normalized=core::normalizeQueuePositions(inputs,events);
    if(!normalized){return fail(normalized.error()==core::QueuePositionsError::Memory?ErrorCode::Io:ErrorCode::Unsupported,
        "Original positions need unsupported ambiguous/provider normalization");}
    LibraryPositionNormalization result;result.source=file->recoverySource();result.positions.reserve(inputs.size());
    // Return in observation order: the worker can install every position after
    // publication using already allocated owner pointers, without a new lookup.
    for(const auto& observation:observations){
      result.positions.push_back({observation.hash,(*normalized)[static_cast<std::size_t>(tokens.at(observation.hash.hex())-1)]});
    }
    std::map<std::size_t,std::string> edited;
    for(std::size_t i=0;i<saved->downloads.size();++i){
      const auto& row=saved->downloads[i];const auto position=(*normalized)[i];
      if(row.position&&*row.position==position){continue;}
      const auto& node=saved->legacyMap?file->document().root().children.at(row.sourceIndex*2+1):file->document().find("downloads")->children.at(row.sourceIndex);
      auto document=core::Document::parse(raw(file->document(),node),compatibleConfigLimits());
      if(!document||!document->replaceValue("position",integer(position))){return fail(ErrorCode::InvalidFormat,"Cannot sparsely normalize original position");}
      edited.emplace(row.sourceIndex,document->serialize());
    }
    if(edited.empty()){return result;}
    if(saved->legacyMap){
      for(const auto& row:saved->downloads){const auto found=edited.find(row.sourceIndex);if(found!=edited.end()&&!file->document().replaceValue(*row.legacyKey,found->second)){return fail(ErrorCode::InvalidFormat,"Cannot normalize legacy position");}}
    }else{
      std::string rows="l";const auto& nodes=file->document().find("downloads")->children;
      for(std::size_t i=0;i<nodes.size();++i){const auto found=edited.find(i);rows+=found==edited.end()?raw(file->document(),nodes[i]):found->second;}rows+='e';
      if(!file->document().replaceValue("downloads",std::move(rows))){return fail(ErrorCode::InvalidFormat,"Cannot normalize saved positions");}
    }
    const auto reparsed=core::Document::parse(file->document().serialize(),compatibleConfigLimits());
    if(!reparsed){return fail(ErrorCode::InvalidFormat,"Normalized inventory encoding failed validation");}
    const auto checked=core::readLibrary(*reparsed);
    if(!checked||checked->downloads.size()!=inputs.size()){return fail(ErrorCode::InvalidFormat,"Normalized inventory failed validation");}
    for(std::size_t i=0;i<checked->downloads.size();++i){if(checked->downloads[i].hash!=saved->downloads[i].hash||checked->downloads[i].position!=(*normalized)[i]){return fail(ErrorCode::InvalidFormat,"Normalized position roundtrip failed");}}
    if(saveAttempted){*saveAttempted=true;}
    const auto committed=file->save(lease);if(!committed){return std::unexpected(committed.error());}result.changed=true;return result;
  }catch(const std::bad_alloc&){return fail(ErrorCode::Io,"Insufficient memory normalizing queue positions; inspect publication before retry");}
}
} // namespace biglybtc::storage
