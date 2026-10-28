#include "queued_mainline_scrape.hpp"
#include "library_view.hpp"
#include "managed_storage.hpp"
#include "managed_tracker_responses.hpp"
#include "queue_positions.hpp"
#include "original_filename_policy.hpp"
#include "biglybtc/saved_dnd.hpp"
#include "torrent_state.hpp"
#include "original_torrent_identity_lt.hpp"
#include <libtorrent/torrent_info.hpp>
#include <openssl/evp.h>
#include <algorithm>
#include <array>
#include <limits>
#include <set>
#include <sys/stat.h>

namespace biglybtc::engine {
namespace {
using Node=core::Node;using Code=storage::ErrorCode;
std::unexpected<storage::Error> error(Code code,const char* text){return std::unexpected(storage::Error{code,text});}
bool unique(const Node& dict){
  if(dict.type!=Node::Type::Dictionary||dict.children.size()%2){return false;}
  std::set<std::string_view> keys;
  for(std::size_t i=0;i<dict.children.size();i+=2){if(dict.children[i].type!=Node::Type::Bytes||!keys.insert(dict.children[i].bytes).second){return false;}}
  return true;
}
bool validList(const Node* value){
  return !value||(value->type==Node::Type::List&&value->children.size()<=16&&std::ranges::all_of(value->children,[](const auto& item){return item.type==Node::Type::Bytes&&item.bytes.size()<=128;}));
}
bool listIncludes(const Node* value,std::string_view needle,bool absent){
  if(!value){return absent;}
  if(value->type!=Node::Type::List||value->children.size()>16){return false;}
  bool present=false;
  for(const auto& item:value->children){if(item.type!=Node::Type::Bytes){return false;}present=present||item.bytes==needle;}return present;
}
bool withoutRealTracker(const core::Document& source,const core::TorrentHash& key){
  std::string hex=key.hex();std::ranges::transform(hex,hex.begin(),[](unsigned char c){return c>='A'&&c<='Z'?static_cast<char>(c+32):static_cast<char>(c);});
  const auto lowerMarker="dht://"+hex+".dht/announce";
  std::ranges::transform(hex,hex.begin(),[](unsigned char c){return c>='a'&&c<='f'?static_cast<char>(c-32):static_cast<char>(c);});
  const auto marker="dht://"+hex+".dht/announce";
  const auto allowed=[&](const Node& url){return url.type==Node::Type::Bytes&&(url.bytes.empty()||url.bytes==marker||url.bytes==lowerMarker);};
  if(const auto* announce=source.find("announce");announce&&!allowed(*announce)){return false;}
  if(const auto* tiers=source.find("announce-list")){
    if(tiers->type!=Node::Type::List||tiers->children.size()>64){return false;}
    for(const auto& tier:tiers->children){if(tier.type!=Node::Type::List||tier.children.size()>64){return false;}
      for(const auto& url:tier.children){if(!allowed(url)){return false;}}}
  }return true;
}
std::optional<std::string> singleConfiguredTracker(const core::Document& source){
  std::optional<std::string> one;bool valid=true;
  const auto take=[&](const Node& node){
    if(node.type!=Node::Type::Bytes||node.bytes.size()>2048){valid=false;return;}
    if(node.bytes.empty()){return;}
    if(node.bytes.find('\0')!=std::string::npos||(!node.bytes.starts_with("http://")&&!node.bytes.starts_with("https://")&&!node.bytes.starts_with("udp://"))||(one&&*one!=node.bytes)){valid=false;return;}one=node.bytes;
  };
  if(const auto* primary=source.find("announce")){take(*primary);}
  if(const auto* tiers=source.find("announce-list")){
    if(tiers->type!=Node::Type::List||tiers->children.size()>64){return {};}
    for(const auto& tier:tiers->children){if(tier.type!=Node::Type::List||tier.children.size()>64){return {};}for(const auto& url:tier.children){take(url);}}
  }return valid?one:std::nullopt;
}
std::optional<std::string> digest(std::string_view bytes){
  std::array<unsigned char,32> out{};unsigned count=0;
  if(EVP_Digest(bytes.data(),bytes.size(),out.data(),&count,EVP_sha256(),nullptr)!=1||count!=out.size()){return {};}
  constexpr char hex[]="0123456789abcdef";std::string result;result.reserve(64);
  for(auto value:out){result+=hex[value>>4];result+=hex[value&15];}return result;
}
void append(std::string& scope,std::string_view value){scope+=std::to_string(value.size())+":";scope+=value;}
std::expected<bool,storage::Error> activeScrapeOnly(const Node& attributes){
  const auto* options=attributes.find("pluginoptions");if(!options){return false;}
  if(!unique(*options)){return error(Code::InvalidFormat,"Ambiguous active Mainline plugin options");}
  const auto* plugin=options->find("mldht");if(!plugin){return false;}
  if(!unique(*plugin)){return error(Code::InvalidFormat,"Ambiguous active mlDHT options");}
  const auto* enabled=plugin->find("enableannounce");if(!enabled){return false;}
  if(enabled->type!=Node::Type::Integer){return error(Code::InvalidFormat,"Invalid active mlDHT announce option");}
  return static_cast<std::uint32_t>(enabled->integer)==0; // Java Number.intValue().
}
std::int64_t later(std::int64_t now,std::int64_t delay){return now>INT64_MAX-delay?INT64_MAX:now+delay;}
}
std::expected<std::vector<SavedMainlineScrapeTarget>,storage::Error>
readSavedMainlineScrapeTargets(const storage::ProfileLease& lease){
  return readCurrentMainlineScrapeTargets(lease,{});
}
std::expected<std::vector<SavedMainlineScrapeTarget>,storage::Error>
readCurrentMainlineScrapeTargets(const storage::ProfileLease& lease,
    std::span<const ManagedMainlineScrapeObservation> observations,bool configuredCandidates){
  try{
    if(observations.size()>256){return error(Code::Unsupported,"Active Mainline scrape target bound");}
    std::map<std::string,const ManagedMainlineScrapeObservation*> active;
    std::set<std::uint64_t> admissions;
    for(const auto& observation:observations){
      if(!observation.storage||!observation.admission
          ||observation.storage->purpose()!=detail::ManagedStoragePurpose::Download
          ||observation.storage->identity().originalKey.bytes().size()!=20
          ||!admissions.insert(observation.admission).second
          ||!active.emplace(observation.storage->identity().originalKey.hex(),&observation).second){
        return error(Code::Conflict,"Ambiguous managed Mainline scrape observation");
      }
    }
    struct stat profile{};
    if(!lease.valid()||::fstat(lease.directoryHandle(),&profile)!=0||!S_ISDIR(profile.st_mode)){return error(Code::Conflict,"Saved scrape profile lease is unavailable");}
    auto inventory=storage::ConfigFile::load(lease,"downloads.config");
    if(!inventory){return std::unexpected(inventory.error());}
    if(inventory->recoverySource()!=storage::RecoverySource::Primary){return error(Code::Conflict,"Recover saved inventory before Mainline scraping");}
    auto library=storage::readLibraryView(lease);if(!library){return std::unexpected(library.error());}
    if(!library->inventoryIssues.empty()||!library->cacheIssues.empty()||library->cacheWarning){return error(Code::InvalidFormat,"Ambiguous saved Mainline scrape authority");}
    std::set<std::string> paused,seen;
    for(const auto& pause:library->pauses){if(!paused.insert(pause.hash.hex()).second){return error(Code::Conflict,"Duplicate saved Mainline pause");}}
    auto store=storage::TorrentStateStore::open(lease);if(!store){return std::unexpected(store.error());}
    std::vector<SavedMainlineScrapeTarget> result;
    for(const auto& row:library->items){
      if(!row.saved.hash){continue;}const auto& key=*row.saved.hash;
      if(!seen.insert(key.hex()).second){return error(Code::Conflict,"Duplicate saved Mainline identity");}
      const auto admitted=active.find(key.hex());
      const auto* observation=admitted==active.end()?nullptr:admitted->second;
      if(!row.saved.persistent||paused.contains(key.hex())){continue;}
      if(observation){
        // Current checked Downloading/Seeding comes from the owner, not this
        // persisted number. Stop/error/unknown saved intent still withdraws it.
        if(row.saved.originalState!=0&&row.saved.originalState!=50
            &&row.saved.originalState!=60&&row.saved.originalState!=75){continue;}
      }else if(row.saved.startup!=core::StartupState::Queued||row.saved.originalState!=75||row.saved.forceStart){continue;}
      if(result.size()==256){return error(Code::Unsupported,"Saved Mainline scrape target bound");}
      const Node* savedRow=nullptr;
      if(row.saved.legacyKey){
        const auto index=row.saved.sourceIndex*2+1;
        if(index<inventory->document().root().children.size()){savedRow=&inventory->document().root().children[index];}
      }else if(const auto* downloads=inventory->document().find("downloads");downloads&&downloads->type==Node::Type::List&&row.saved.sourceIndex<downloads->children.size()){
        savedRow=&downloads->children[row.saved.sourceIndex];
      }
      if(!savedRow||!unique(*savedRow)){return error(Code::InvalidFormat,"Ambiguous saved Mainline row");}
      // Actual native intake intent belongs to downloads.config, not attrs.
      // Pending/released verification and allocation remain separate consumers.
      if(savedRow->find("BiglyBTC.verifiedSeeding")||savedRow->find("BiglyBTC.allocationMode")){continue;}
      auto state=store->loadAttributes(lease,key);if(!state){return std::unexpected(state.error());}
      if(state->recoveredBackup()){continue;}
      const auto& doc=state->document();const auto* info=doc.find("info");const auto* attrs=doc.find("attributes");
      if(!unique(doc.root())||!info||!unique(*info)||!attrs||!unique(*attrs)){return error(Code::InvalidFormat,"Invalid saved Mainline metadata/attributes");}
      if(const auto* privateFlag=info->find("private")){if(privateFlag->type!=Node::Type::Integer||privateFlag->integer!=0){continue;}}
      if(!validList(attrs->find("networks"))||!validList(attrs->find("peersources"))||!validList(attrs->find("peersourcesdenied"))){return error(Code::InvalidFormat,"Invalid saved Mainline permission list");}
      if(!listIncludes(attrs->find("networks"),"Public",true)||!listIncludes(attrs->find("peersources"),"DHT",true)
          ||listIncludes(attrs->find("peersourcesdenied"),"DHT",false)){continue;}
      if(const auto* azureus=doc.find("azureus_properties")){
        if(!unique(*azureus)){return error(Code::InvalidFormat,"Invalid saved Mainline backup permission");}
        if(const auto* backup=azureus->find("dht_backup_enable");backup&&(backup->type!=Node::Type::Integer||backup->integer!=1)){continue;}
      }
      if(const auto* flags=attrs->find("flags");flags&&(flags->type!=Node::Type::Integer||(static_cast<std::uint64_t>(flags->integer)&0x200u))){continue;}
      std::optional<std::string> configured;
      if(!withoutRealTracker(doc,key)){
        if(!configuredCandidates){continue;}configured=singleConfiguredTracker(doc);
        // This returns an authenticated candidate, not network permission.
        // The sole owner additionally requires its actual library response.
        if(!configured||!listIncludes(attrs->find("peersources"),"Tracker",true)
            ||listIncludes(attrs->find("peersourcesdenied"),"Tracker",false)){continue;}
      }
      if(observation){
        const auto permitted=activeScrapeOnly(*attrs);if(!permitted){return std::unexpected(permitted.error());}
        if(!*permitted){continue;} // Normal active announce mode is not a passive scrape.
      }
      const auto encoded=doc.serialize();libtorrent::error_code ec;
      libtorrent::torrent_info metadata(libtorrent::span<const char>(encoded.data(),encoded.size()),ec,libtorrent::from_span);
      if(ec){return error(Code::InvalidFormat,"Saved scrape metadata is not accepted by the engine");}
      auto identity=storage::originalTorrentIdentityFromLibtorrent(doc,metadata);
      if(!identity){return std::unexpected(identity.error());}
      auto bound=storage::validateOriginalTorrentKey(*identity,key,true);if(!bound){return std::unexpected(bound.error());}
      auto layout=storage::readOriginalLayout(lease,doc);if(!layout){return std::unexpected(layout.error());}
      bool noSeeds=false;
      std::string mapping;
      if(observation){
        const auto& files=*observation->storage;const auto& captured=files.capturedDownload();
        const auto& capturedIdentity=files.identity();
        // A fresh passive permission cannot turn an already discovery-enabled
        // admission into both consumers. Route changes require normal readmission.
        if(files.mainlinePolicy().discover||files.mainlinePolicy().announce){continue;}
        const auto params=files.parameters();if(!params.ti){continue;}
        const auto subject=storage::originalTorrentInfoSubject(doc);if(!subject){return std::unexpected(subject.error());}
        const auto section=params.ti->info_section();
        if(captured.hash!=row.saved.hash||!captured.persistent
            ||captured.torrentFile!=row.saved.torrentFile||captured.saveDirectory!=row.saved.saveDirectory
            ||captured.saveFile!=row.saved.saveFile||captured.legacySavePath!=row.saved.legacySavePath
            ||capturedIdentity.originalKey!=identity->originalKey||capturedIdentity.selected!=identity->selected
            ||capturedIdentity.full!=identity->full
            ||std::string_view(section.data(),static_cast<std::size_t>(section.size()))!=*subject){continue;}
        auto currentMapping=storage::originalQueueMappingScope(doc);if(!currentMapping){return std::unexpected(currentMapping.error());}
        const auto currentPolicy=compat::originalFilePolicy(*savedRow,files.capturedFilePolicy().size());
        if(files.capturedQueueMappingScope().empty()||*currentMapping!=files.capturedQueueMappingScope()
            ||!currentPolicy||!std::ranges::equal(*currentPolicy,files.capturedFilePolicy())){continue;}
        mapping=std::move(*currentMapping);noSeeds=observation->wholeComplete;
      }else{
        if(!row.saved.completedBytes||*row.saved.completedBytes<0||*row.saved.completedBytes>layout->bytes){continue;}
        const auto progress=core::projectSavedDndProgress(layout->bytes,row.saved.completedBytes,core::readSavedDndFacts(attrs));
        if(*row.saved.completedBytes==layout->bytes&&!progress.controllerNoDiskCompleteIncludingDnd){continue;}
        noSeeds=progress.controllerNoDiskCompleteIncludingDnd.value_or(false);
      }
      // Saved targets use the original no-DiskManager query preference;
      // active targets use the SAME accepted whole-complete sample. Neither
      // branch grants a piece/resume bit or any seeding/transfer authority.
      // Full32 input stays a reader policy; BEP5 queries always use original20.
      if(key.bytes().size()!=20){continue;}
      auto raw=storage::originalTorrentInfoSubject(doc);if(!raw){return std::unexpected(raw.error());}
      std::string scope;append(scope,std::to_string(profile.st_dev));append(scope,std::to_string(profile.st_ino));append(scope,key.hex());append(scope,identity->selected==storage::OriginalSelectedHash::V1?"v1":"v2");append(scope,*raw);append(scope,noSeeds?"noseed1":"noseed0");append(scope,row.saved.torrentFile);
      append(scope,row.saved.saveDirectory);append(scope,row.saved.saveFile.value_or(""));
      for(auto name:{"networks","peersources","peersourcesdenied","pluginoptions"}){
        if(const auto* value=attrs->find(name)){auto encodedValue=core::encodeValue(*value,storage::compatibleConfigLimits());if(!encodedValue){return error(Code::InvalidFormat,"Scrape scope exceeds encoding bound");}append(scope,*encodedValue);}else{append(scope,"");}
      }
      if(const auto* az=doc.find("azureus_properties")){auto value=core::encodeValue(*az,storage::compatibleConfigLimits());if(!value){return error(Code::InvalidFormat,"Scrape source scope exceeds encoding bound");}append(scope,*value);}
      if(configured){append(scope,"single-configured-tracker");append(scope,*configured);}
      if(observation){append(scope,"active-mldht-scrape");append(scope,std::to_string(observation->admission));append(scope,mapping);}
      auto hashed=digest(scope);if(!hashed){return error(Code::Io,observation?"Cannot authenticate managed Mainline scope":"Cannot authenticate saved Mainline scope");}
      std::string responseKey;
      if(configured){const auto ownerKey=detail::originalTrackerResponseKey(lease,*identity,*raw,*configured);if(!ownerKey){continue;}responseKey=*ownerKey;}
      result.push_back({key,identity->full,std::move(*hashed),noSeeds,observation?observation->admission:0,std::move(responseKey),configured.value_or("")});
    }
    auto current=storage::ConfigFile::load(lease,"downloads.config");
    if(!current||current->recoverySource()!=storage::RecoverySource::Primary
        ||current->document().originalBytes()!=inventory->document().originalBytes()){
      return error(Code::Conflict,"Saved Mainline inventory changed while authenticating targets");
    }
    return result;
  }catch(...){return error(Code::Io,"Cannot read saved Mainline scrape authority");}
}
std::int64_t QueuedMainlineScrapes::delayMillis(bool initial,std::uint32_t random)noexcept{
  return initial?60000+static_cast<std::int64_t>(random%60)*1000:1200000+static_cast<std::int64_t>(random%600)*1000;
}
bool QueuedMainlineScrapes::checkDue(std::int64_t now)noexcept{
  if(now<lastNow_){rows_.clear();active_.reset();activeKey_.clear();checkAt_=INT64_MAX;return false;}
  lastNow_=now;if(now<checkAt_){return false;}checkAt_=later(now,10000);return true;
}
std::expected<void,storage::Error> QueuedMainlineScrapes::reconcile(std::span<const SavedMainlineScrapeTarget> targets,
    std::int64_t now,std::uint32_t random){
  if(targets.size()>256||now<0){return error(Code::InvalidFormat,"Invalid saved scrape target inventory");}
  std::set<std::string> incoming;
  for(const auto& target:targets){if(target.hash.bytes().size()!=20||target.scope.size()!=64||!incoming.insert(target.hash.hex()).second){return error(Code::InvalidFormat,"Ambiguous saved scrape target");}}
  // Eligible-target absence is not a manual acknowledgement: it may just
  // reflect a disabled route/source. Only explicit freshly read settlement
  // clears a tombstone. The cap counts outstanding unacknowledged revokes.
  if(revokedOverflow_){rows_.clear();return error(Code::Busy,"Mainline outstanding manual revocation bound");}
  std::map<std::string,Item> next;
  for(const auto& target:targets){
    const auto key=target.hash.hex();const auto revoked=revoked_.find(key);
    if(revoked!=revoked_.end()&&(revoked->second=="*"||revoked->second==target.scope)){continue;}
    auto old=rows_.find(key);
    if(old!=rows_.end()&&old->second.target.scope==target.scope){next.emplace(key,std::move(old->second));}
    else{next.emplace(key,Item{target,later(now,delayMillis(true,random++)),0,{}});}
  }
  rows_=std::move(next);return {};
}
std::optional<SavedMainlineScrapeTarget> QueuedMainlineScrapes::due(std::int64_t now)const{
  if(active_){return {};}
  const Item* earliest=nullptr;
  for(const auto& [key,row]:rows_){(void)key;if(row.due<=now&&(!earliest||row.due<earliest->due)){earliest=&row;}}
  return earliest?std::optional{earliest->target}:std::nullopt;
}
bool QueuedMainlineScrapes::started(const core::TorrentHash& hash,std::string_view scope,std::uint64_t round,std::int64_t wall){
  const auto found=rows_.find(hash.hex());
  if(active_||!round||wall<0||found==rows_.end()||found->second.target.scope!=scope){return false;}
  active_=round;activeKey_=hash.hex();activeScope_=scope;found->second.wall=wall;return true;
}
std::optional<SavedMainlineScrapeTarget> QueuedMainlineScrapes::activeTarget()const{
  const auto row=rows_.find(activeKey_);return active_&&row!=rows_.end()&&row->second.target.scope==activeScope_?std::optional{row->second.target}:std::nullopt;
}
void QueuedMainlineScrapes::completed(std::uint64_t round,const std::optional<MainlineDhtScrape>& result,
    const std::optional<SavedMainlineScrapeTarget>& fresh,std::int64_t now,std::uint32_t random){
  if(!active_||*active_!=round){return;}
  const auto found=rows_.find(activeKey_);
  if(found!=rows_.end()){
    auto& row=found->second;row.due=later(now,delayMillis(false,random));
    if(fresh&&fresh->hash==row.target.hash&&fresh->full==row.target.full&&fresh->admission==row.target.admission&&fresh->scope==activeScope_
        &&row.target.scope==activeScope_&&result&&result->round==round
        &&std::equal(fresh->hash.bytes().begin(),fresh->hash.bytes().end(),reinterpret_cast<const std::byte*>(result->hash.data()))
        &&result->dialect==services::mainline_scrape::Dialect::OriginalBFse
        &&(result->summary.seeds>0||result->summary.peers>0)){
      row.value=MainlineScrapeEstimate{row.target.hash,row.target.scope,round,row.wall,result->summary};
    }else if(!fresh||fresh->scope!=activeScope_){row.value.reset();}
  }
  active_.reset();activeKey_.clear();activeScope_.clear();
}
void QueuedMainlineScrapes::revoke(const core::TorrentHash& hash)noexcept{
  try{
    const auto key=hash.hex();const auto row=rows_.find(key);
    if(!revoked_.contains(key)&&revoked_.size()==256){revokedOverflow_=true;rows_.clear();return;}
    revoked_.insert_or_assign(key,row==rows_.end()?"*":row->second.target.scope);
    if(row!=rows_.end()){rows_.erase(row);}
    if(activeKey_==key){active_.reset();activeKey_.clear();activeScope_.clear();}
  }catch(...){revokedOverflow_=true;rows_.clear();active_.reset();activeKey_.clear();activeScope_.clear();}
}
bool QueuedMainlineScrapes::revoked(const core::TorrentHash& hash)const noexcept{
  try{return revokedOverflow_||revoked_.contains(hash.hex());}catch(...){return true;}
}
void QueuedMainlineScrapes::settled(const core::TorrentHash& hash){
  const auto key=hash.hex();revoked_.erase(key);rows_.erase(key);
  if(activeKey_==key){active_.reset();activeKey_.clear();activeScope_.clear();}
}
std::vector<MainlineScrapeEstimate> QueuedMainlineScrapes::estimates()const{
  std::vector<MainlineScrapeEstimate> result;for(const auto& [key,row]:rows_){(void)key;if(row.value){result.push_back(*row.value);}}return result;
}
} // namespace biglybtc::engine
