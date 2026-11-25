// SPDX-License-Identifier: GPL-2.0-or-later
#include "durable_intake_receipt.hpp"
#include "encoded_document.hpp"
#include "profile_file_snapshot.hpp"
#include "../platform/utf8_path.hpp"
#include "original_filename_policy.hpp"
#include "original_torrent_identity.hpp"
#include <openssl/evp.h>
#include "biglybtc/state_cache.hpp"
#include <array>
#include <thread>

namespace biglybtc::storage {
namespace {
using detail::HistorySource;using detail::captureHistorySource;using Clock=std::chrono::steady_clock;
void require(bool ok,ErrorCode code,const char* message){if(!ok){throw Error{code,message};}}
template<class T>T take(std::expected<T,Error> v){if(!v){throw v.error();}return std::move(*v);}
core::Document document(const std::string& bytes){core::Limits limits;limits.inputBytes=64U*1024U*1024U;auto value=EncodedDocument::parse(bytes,limits);require(bool(value)&&value->document().root().type==core::Node::Type::Dictionary,ErrorCode::InvalidFormat,"Invalid committed intake document");return value->document();}
std::int64_t number(const core::Node* map,std::string_view key,std::int64_t absent=0){if(!map){return absent;}require(map->type==core::Node::Type::Dictionary,ErrorCode::InvalidFormat,"Invalid committed intake attributes");const auto* value=map->find(key);if(!value){return absent;}require(value->type==core::Node::Type::Integer,ErrorCode::InvalidFormat,"Invalid original history numeric input");return value->integer;}
std::string raw(const auto& value){return {reinterpret_cast<const char*>(value.data()),value.size()};}
}
struct DurableIntakeReceipt::Data {
  std::shared_ptr<const ProfileLease> lease;std::thread::id worker=std::this_thread::get_id();
  std::uint64_t epoch=0;Clock::time_point end;std::vector<HistorySource> sources;
  core::DownloadHistoryNewRecord record;bool monitored=false;
  std::optional<core::SavedTagObserveSwarm> swarmTags;
  core::TorrentHash subject;
};
DurableIntakeReceipt::DurableIntakeReceipt(std::unique_ptr<Data> data):data_(std::move(data)){}
DurableIntakeReceipt::DurableIntakeReceipt(DurableIntakeReceipt&&)noexcept=default;
DurableIntakeReceipt& DurableIntakeReceipt::operator=(DurableIntakeReceipt&&)noexcept=default;
DurableIntakeReceipt::~DurableIntakeReceipt()=default;
std::expected<DurableIntakeReceipt,Error> DurableIntakeReceipt::issue(const ProfileLease& lease,const core::TorrentHash& hash,std::string_view acceptedInventory,std::string_view acceptedState,std::string_view acceptedArchive,bool cachedSwarmTags){try{
  auto access=take(lease.sourceAccess());auto value=std::make_unique<Data>();value->lease=take(lease.retain());value->epoch=lease.sourceRevision();value->end=Clock::now()+std::chrono::seconds(30);
  std::size_t total=0;
  const auto add=[&](std::string name)->std::size_t{auto item=take(captureHistorySource(lease.directoryHandle(),std::move(name)));const auto size=item.bytes?item.bytes->size():0;require(size<=128U*1024U*1024U-total,ErrorCode::Unsupported,"History receipt exceeds bounded owning source budget");total+=size;value->sources.push_back(std::move(item));return value->sources.size()-1;};
  const auto inventory=add("downloads.config");const auto state=add("active/"+hash.hex()+".dat");const auto archive=add("torrents/"+hash.hex()+".torrent");
  for(const auto* name:{"downloads.config.bak","downloads.config.saving","biglybt.config","biglybt.config.bak","biglybt.config.saving","active/cache.dat","active/cache.dat.bak","active/cache.dat.saving"}){add(name);}
  for(const auto* suffix:{".dat.bak",".dat.saving",".dat.import"}){require(!value->sources[add("active/"+hash.hex()+suffix)].bytes,ErrorCode::Conflict,"Fresh history intake state has recovery candidate");}
  for(const auto* suffix:{".torrent.bak",".torrent.saving",".torrent.import"}){require(!value->sources[add("torrents/"+hash.hex()+suffix)].bytes,ErrorCode::Conflict,"Fresh history archive has recovery candidate");}
  for(const auto* name:{"BiglyBTC.torrent-import","BiglyBTC.torrent-import.saving","BiglyBTC.torrent-import.inventory","BiglyBTC.torrent-import.backup"}){require(!value->sources[add(name)].bytes,ErrorCode::Conflict,"Fresh history intake journal remains");}
  require(value->sources[inventory].bytes&&value->sources[state].bytes&&value->sources[archive].bytes,ErrorCode::Missing,"Durable intake history sources absent");
  require(*value->sources[inventory].bytes==acceptedInventory&&*value->sources[state].bytes==acceptedState&&*value->sources[archive].bytes==acceptedArchive,ErrorCode::Conflict,"Committed intake postimage changed before history receipt");
  auto list=document(*value->sources[inventory].bytes);auto saved=core::readLibrary(list);require(saved&&saved->issues.empty(),ErrorCode::InvalidFormat,"Ambiguous committed inventory");
  const core::SavedDownload* selected=nullptr;for(const auto& row:saved->downloads){if(row.hash&&*row.hash==hash){require(!selected,ErrorCode::Conflict,"Duplicate committed intake row");selected=&row;}}require(selected,ErrorCode::Missing,"Committed intake row absent");
  const HistorySource* cache=nullptr;const HistorySource* cacheBackup=nullptr;
  for(const auto& item:value->sources){if(item.name=="active/cache.dat"){cache=&item;}if(item.name=="active/cache.dat.bak"){cacheBackup=&item;}}
  require(cache&&cacheBackup&& (cache->bytes||!cacheBackup->bytes),ErrorCode::Conflict,"Original selected cache primary unavailable");
  if(cache->bytes){auto cached=document(*cache->bytes);auto projection=core::readStateCache(cached);require(projection&&projection->issues.empty()&&!projection->torrents.contains(hash.hex()),ErrorCode::Conflict,"Fresh history row has conflicting cache authority");}
  auto original=document(*value->sources[state].bytes);auto source=document(*value->sources[archive].bytes);auto info=take(originalTorrentInfoSubject(original));require(info==take(originalTorrentInfoSubject(source)),ErrorCode::Conflict,"Committed intake archive/info mismatch");
  OriginalFullInfoHashes full;const auto* infoNode=source.find("info");
  if(infoNode&&infoNode->find("pieces")){full.v1.emplace();unsigned size=0;require(EVP_Digest(info.data(),info.size(),reinterpret_cast<unsigned char*>(full.v1->data()),&size,EVP_sha1(),nullptr)==1&&size==full.v1->size(),ErrorCode::Io,"History v1 digest failed");}
  if(const auto* version=infoNode?infoNode->find("meta version"):nullptr;version&&version->type==core::Node::Type::Integer&&version->integer==2){full.v2.emplace();unsigned size=0;require(EVP_Digest(info.data(),info.size(),reinterpret_cast<unsigned char*>(full.v2->data()),&size,EVP_sha256(),nullptr)==1&&size==full.v2->size(),ErrorCode::Io,"History v2 digest failed");}
  const auto identity=take(selectOriginalTorrentIdentity(source,{info,std::move(full)}));require(identity.originalKey==hash,ErrorCode::Conflict,"Committed history full identity mismatch");
  const auto context=take(readOriginalFilenameContext(lease));auto layout=compat::originalLayout(original,100000,context.conversions,context.decoderShowAll);require(bool(layout),ErrorCode::InvalidFormat,"Committed original layout unavailable");
  const auto* attrs=original.find("attributes");const auto* params=attrs?attrs->find("parameters"):nullptr;
  value->subject=hash;
  if (cachedSwarmTags) {
    const auto* names = attrs ? attrs->find("stag") : nullptr;
    require(names && names->type==core::Node::Type::List && !names->children.empty()
        && names->children.size()<=128,ErrorCode::InvalidFormat,"Committed Swarm Tag list unavailable");
    core::SavedTagObserveSwarm observation{{},std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count(),{}};
    for (const auto& name : names->children) {
      require(name.type==core::Node::Type::Bytes && !name.bytes.empty() && name.bytes.size()<=128,
          ErrorCode::InvalidFormat,"Committed Swarm Tag name unavailable");
      observation.names.emplace_back(name.bytes);
    }
    value->swarmTags.emplace(std::move(observation));
  }
  const auto flags=number(attrs,"flags");value->monitored=core::originalDownloadHistoryMonitored(selected->persistent,static_cast<std::uint64_t>(flags));
  auto& row=value->record;row.uid=core::originalDownloadHistoryUid(raw(hash.bytes()),number(attrs,"stats.download.added.time"));
  row.hash=identity.full.v1?std::optional<std::string>{raw(*identity.full.v1)}:std::optional<std::string>{raw(hash.bytes())};if(identity.full.v2){row.hashV2=raw(*identity.full.v2);}
  row.name=layout->name;if(const auto* name=attrs?attrs->find("displayname"):nullptr){require(name->type==core::Node::Type::Bytes,ErrorCode::InvalidFormat,"Invalid original display name");if(!name->bytes.empty()){row.name=name->bytes;}}
  const auto directory=platform::pathFromUtf8(selected->saveDirectory);require(selected->saveFile&&directory.is_absolute(),ErrorCode::InvalidFormat,"Modern committed save location required");row.saveLocation=platform::pathToUtf8(directory/platform::pathFromUtf8(*selected->saveFile));row.size=layout->bytes;
  row.addTime=number(params,"stats.download.added.time");row.completeTime=core::originalDownloadHistoryCompleteTime(number(attrs,"complt"),number(params,"stats.download.completed.time"));
  // Validate bounded UTF8/record encoding without publishing or inventing any times.
  require(bool(core::editSavedDownloadHistory("de",core::DownloadHistoryAppend{row})),ErrorCode::Unsupported,"Original intake history fields exceed supported record bounds");
  for(const auto& item:value->sources){require(item.matches(take(captureHistorySource(lease.directoryHandle(),item.name))),ErrorCode::Conflict,"History intake sources changed while issuing receipt");}
  return DurableIntakeReceipt(std::move(value));
}catch(const Error& e){return std::unexpected(e);}catch(...){return std::unexpected(Error{ErrorCode::Io,"Import saved; history receipt unavailable for review"});}}
std::expected<void,Error> DurableIntakeReceipt::current(const ProfileLease& lease,bool exactEpoch)const{try{
  require(data_&&data_->worker==std::this_thread::get_id(),ErrorCode::Conflict,"Missing/consumed/foreign-worker history intake receipt");
  require(data_->lease->sharesOwnership(lease),ErrorCode::Conflict,"Foreign history intake OFD");(void)take(lease.retain());
  require(Clock::now()<data_->end&&(!exactEpoch||lease.sourceRevision()==data_->epoch),ErrorCode::Conflict,"Stale intake history source/receipt deadline");
  for(const auto& item:data_->sources){require(item.matches(take(captureHistorySource(lease.directoryHandle(),item.name))),ErrorCode::Conflict,"Committed intake history source changed");}return {};
}catch(const Error& e){return std::unexpected(e);}catch(...){return std::unexpected(Error{ErrorCode::Io,"Cannot validate history intake sources"});}}
bool DurableIntakeReceipt::hasSwarmTagObservation()const noexcept {
  return data_ && data_->swarmTags.has_value();
}
std::expected<std::optional<core::SavedTagObserveSwarm>,Error> DurableIntakeReceipt::swarmTagObservation(
    const ProfileLease& lease,const core::TorrentHash& hash)const {
  try {
    require(data_ && hash==data_->subject,ErrorCode::Conflict,"Swarm Tags belong to another import");
    if (!data_->swarmTags) { return std::optional<core::SavedTagObserveSwarm>{}; }
    const auto source=current(lease,true);
    if (!source) { return std::unexpected(source.error()); }
    return data_->swarmTags;
  } catch (const Error& error) {
    return std::unexpected(error);
  } catch (...) {
    return std::unexpected(Error{ErrorCode::Io,"Cannot capture committed Swarm Tags"});
  }
}
const core::DownloadHistoryNewRecord& DurableIntakeReceipt::record()const{return data_->record;}
bool DurableIntakeReceipt::monitored()const{return data_->monitored;}
} // namespace biglybtc::storage
