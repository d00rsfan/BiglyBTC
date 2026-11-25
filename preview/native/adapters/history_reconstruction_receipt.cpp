// SPDX-License-Identifier: GPL-2.0-or-later
#include "../platform/utf8_path.hpp"
#include "history_reconstruction_receipt.hpp"
#include "encoded_document.hpp"
#include "profile_file_snapshot.hpp"
#include "original_filename_policy.hpp"
#include "original_torrent_identity.hpp"
#include "torrent_state.hpp"
#include "biglybtc/state_cache.hpp"
#include <openssl/evp.h>
#include <array>
#include <algorithm>
#include <set>
#include <thread>
namespace biglybtc::storage {
namespace {
using detail::HistorySource;using detail::StateDescriptor;using detail::StateHandle;using detail::StateStamp;using Clock=std::chrono::steady_clock;
void require(bool ok,ErrorCode code,const char* message){if(!ok){throw Error{code,message};}}
template<class T>T take(std::expected<T,Error> v){if(!v){throw v.error();}return std::move(*v);}
HistorySource capture(StateHandle root,StateHandle active,std::string name) {
  if (!name.starts_with("active/")) { return take(detail::captureHistorySource(root,std::move(name))); }
  if (active==detail::InvalidStateHandle) { return {{},std::move(name)}; }
  auto file=take(detail::captureProfileFile(active,name.substr(7),64U*1024U*1024U));
  return {std::move(file),std::move(name)};
}
core::Document document(const std::string& bytes){core::Limits limits;limits.inputBytes=64U*1024U*1024U;auto value=EncodedDocument::parse(bytes,limits);require(bool(value)&&value->document().root().type==core::Node::Type::Dictionary,ErrorCode::InvalidFormat,"Invalid reconstruction document");return value->document();}
const core::Node* one(const core::Node* map,std::string_view key){
  if(!map){return nullptr;}require(map->type==core::Node::Type::Dictionary,ErrorCode::InvalidFormat,"Invalid reconstruction attributes");const core::Node* value=nullptr;
  for(std::size_t n=0;n+1<map->children.size();n+=2){if(map->children[n].bytes==key){require(!value,ErrorCode::InvalidFormat,"Duplicate relevant reconstruction field");value=&map->children[n+1];}}return value;
}
std::int64_t number(const core::Node* map,std::string_view key,std::int64_t absent=0){const auto* value=one(map,key);if(!value){return absent;}require(value->type==core::Node::Type::Integer,ErrorCode::InvalidFormat,"Invalid original history numeric input");return value->integer;}
std::string raw(const auto& value){return {reinterpret_cast<const char*>(value.data()),value.size()};}
void budget(std::stop_token stop,Clock::time_point end){require(!stop.stop_requested()&&Clock::now()<end,ErrorCode::Cancelled,"History reconstruction cancelled/deadline");}
}
struct HistoryReconstructionReceipt::Data {
  std::shared_ptr<const ProfileLease> lease;std::thread::id worker=std::this_thread::get_id();
  std::array<std::uint64_t,3> owner{};std::uint64_t epoch=0;Clock::time_point end;
  StateDescriptor active;std::optional<StateStamp> activeStat;std::vector<HistorySource> sources;
  std::vector<core::DownloadHistoryNewRecord> records;
  bool activeCurrent()const{
    const auto named=take(detail::stateStampAt(lease->directoryHandle(),"active"));
    if (!activeStat) { return !named; }
    const auto held=take(detail::stateStamp(active.get()));
    return named && detail::sameStateDirectory(*activeStat,held)
        && detail::sameStateDirectory(held,*named);
  }
};
HistoryReconstructionReceipt::HistoryReconstructionReceipt(std::unique_ptr<Data> v):data_(std::move(v)){}
HistoryReconstructionReceipt::HistoryReconstructionReceipt(HistoryReconstructionReceipt&&)noexcept=default;
HistoryReconstructionReceipt& HistoryReconstructionReceipt::operator=(HistoryReconstructionReceipt&&)noexcept=default;
HistoryReconstructionReceipt::~HistoryReconstructionReceipt()=default;
std::expected<HistoryReconstructionReceipt,Error> HistoryReconstructionReceipt::issue(const ProfileLease& lease,
    std::array<std::uint64_t,3> owner,std::stop_token stop,Clock::time_point end){try{
  budget(stop,end);require(std::ranges::all_of(owner,[](auto n){return n!=0;}),ErrorCode::Conflict,"History reset requires an actual normal owner identity");
  auto access=take(lease.sourceAccess());auto value=std::make_unique<Data>();value->lease=take(lease.retain());value->owner=owner;value->epoch=lease.sourceRevision();value->end=std::min(end,Clock::now()+std::chrono::seconds(30));
  const auto active=take(detail::stateStampAt(lease.directoryHandle(),"active"));
  if (active) {
    require(active->directory&&active->owned,ErrorCode::UnsafePath,"History reset requires an owned real active directory");
    value->active=take(detail::openStateDirectory(lease.directoryHandle(),"active"));
    const auto held=take(detail::stateStamp(value->active.get()));
    require(detail::sameStateDirectory(*active,held),ErrorCode::Conflict,"Active directory changed while opening");
    value->activeStat=held;
  }
  std::size_t total=0;const auto add=[&](std::string name)->std::size_t{budget(stop,value->end);auto in=capture(lease.directoryHandle(),value->active.get(),std::move(name));const auto size=in.bytes?in.bytes->size():0;require(size<=128U*1024U*1024U-total,ErrorCode::Unsupported,"History reset source byte bound");total+=size;value->sources.push_back(std::move(in));return value->sources.size()-1;};
  const auto inventory=add("downloads.config");for(const auto* name:{"downloads.config.bak","downloads.config.saving","biglybt.config","biglybt.config.bak","biglybt.config.saving","active/cache.dat","active/cache.dat.bak","active/cache.dat.saving"}){add(name);}
  for(const auto* name:{"BiglyBTC.torrent-import","BiglyBTC.torrent-import.saving","BiglyBTC.torrent-import.inventory","BiglyBTC.torrent-import.backup","BiglyBTC.file-operation","BiglyBTC.file-operation.BiglyBTC.fileop-saving","BiglyBTC.category-delete.config","BiglyBTC.category-delete.config.bak","BiglyBTC.category-delete.config.saving",".BiglyBTC.category-delete",".BiglyBTC.category-delete.bak",".BiglyBTC.category-delete.saving"}){require(!value->sources[add(name)].bytes,ErrorCode::Conflict,"History reset overlaps pending original recovery");}
  for(const auto& item:value->sources){if(item.name=="biglybt.config"&&item.bytes){auto settings=document(*item.bytes);const auto* enabled=one(&settings.root(),"Download History Enabled");require(!enabled||(enabled->type==core::Node::Type::Integer&&enabled->integer==1),ErrorCode::Unsupported,"Enable history before explicit Reset");}}
  for(const auto& item:value->sources){if(item.name=="downloads.config.saving"||item.name=="biglybt.config.saving"||item.name=="active/cache.dat.saving"){require(!item.bytes,ErrorCode::Conflict,"History reset source has pending candidate");}}
  require(bool(value->sources[inventory].bytes),ErrorCode::Missing,"History reset requires actual complete saved inventory");auto list=document(*value->sources[inventory].bytes);auto library=core::readLibrary(list);require(library&&library->issues.empty()&&library->downloads.size()<=256,ErrorCode::Unsupported,"History reset requires unambiguous complete inventory within256 rows");
  const HistorySource* cache=nullptr;const HistorySource* cacheBackup=nullptr;for(const auto& item:value->sources){if(item.name=="active/cache.dat"){cache=&item;}if(item.name=="active/cache.dat.bak"){cacheBackup=&item;}}
  require(cache&&cacheBackup&&(cache->bytes||!cacheBackup->bytes),ErrorCode::Conflict,"Resolve missing state-cache primary before Reset");
  if(cache->bytes){auto parsed=document(*cache->bytes);auto projection=core::readStateCache(parsed);require(projection&&projection->issues.empty(),ErrorCode::InvalidFormat,"Ambiguous saved attribute cache");
    // The generic projection is last-wins; reconstruction must not certify
    // conflicting saved state as a complete authoritative backend snapshot.
    if(const auto* entries=one(&parsed.root(),"state")){
      require(entries->type==core::Node::Type::List,ErrorCode::InvalidFormat,"Malformed saved attribute-cache state list");std::set<std::string_view> keys;
      for(const auto& row:entries->children){const auto* key=one(&row,"hash");require(key&&key->type==core::Node::Type::Bytes,ErrorCode::InvalidFormat,"Missing saved attribute-cache identity");require(keys.insert(key->bytes).second,ErrorCode::InvalidFormat,"Duplicate saved attribute-cache identity");}
    }
  }
  std::optional<TorrentStateStore> states;if(!library->downloads.empty()){states.emplace(take(TorrentStateStore::open(lease)));}
  const auto context=take(readOriginalFilenameContext(lease));std::set<std::string> hashes;std::set<std::int64_t> uids;
  for(const auto& saved:library->downloads){budget(stop,value->end);require(saved.hash&&hashes.insert(saved.hash->hex()).second,ErrorCode::Conflict,"History reset inventory has missing/duplicate identities");
    const auto base="active/"+saved.hash->hex();const auto primary=add(base+".dat");add(base+".dat.bak");require(bool(value->sources[primary].bytes),ErrorCode::Missing,"History reset requires selected primary state");
    for(const auto* suffix:{".dat.saving",".dat.import"}){require(!value->sources[add(base+suffix)].bytes,ErrorCode::Conflict,"History reset selected state has pending candidate");}
    auto selected=take(states->loadAttributes(lease,*saved.hash));require(!selected.recoveredBackup(),ErrorCode::Conflict,"History reset does not bless recovered state");const auto& original=selected.document();(void)take(originalTorrentInfoOrder(original));const auto info=take(originalTorrentInfoSubject(original));OriginalFullInfoHashes full;const auto* node=original.find("info");
    if(node&&node->find("pieces")){full.v1.emplace();unsigned n=0;require(EVP_Digest(info.data(),info.size(),reinterpret_cast<unsigned char*>(full.v1->data()),&n,EVP_sha1(),nullptr)==1&&n==full.v1->size(),ErrorCode::Io,"History reset v1 digest failed");}
    if(const auto* version=node?node->find("meta version"):nullptr;version&&version->type==core::Node::Type::Integer&&version->integer==2){full.v2.emplace();unsigned n=0;require(EVP_Digest(info.data(),info.size(),reinterpret_cast<unsigned char*>(full.v2->data()),&n,EVP_sha256(),nullptr)==1&&n==full.v2->size(),ErrorCode::Io,"History reset v2 digest failed");}
    const auto identity=take(selectOriginalTorrentIdentity(original,{info,std::move(full)}));require(identity.originalKey==*saved.hash,ErrorCode::Conflict,"History reset full selected identity mismatch");
    const auto* attrs=one(&original.root(),"attributes");const auto* params=one(attrs,"parameters");const auto flags=number(attrs,"flags");if(!core::originalDownloadHistoryMonitored(saved.persistent,static_cast<std::uint64_t>(flags))){continue;}
    auto layout=compat::originalLayout(original,100000,context.conversions,context.decoderShowAll);require(bool(layout),ErrorCode::Unsupported,"Original reset name/size unavailable");core::DownloadHistoryNewRecord row;
    row.uid=core::originalDownloadHistoryUid(raw(saved.hash->bytes()),number(attrs,"stats.download.added.time"));require(uids.insert(row.uid).second,ErrorCode::Conflict,"Original history UID collision in current library");
    row.hash=identity.full.v1?std::optional<std::string>{raw(*identity.full.v1)}:std::optional<std::string>{raw(saved.hash->bytes())};if(identity.full.v2){row.hashV2=raw(*identity.full.v2);}row.name=layout->name;
    if(const auto* name=one(attrs,"displayname")){require(name->type==core::Node::Type::Bytes,ErrorCode::InvalidFormat,"Malformed original reset display name");row.name=name->bytes;}
    if(!layout->multifile){for(const auto* key:{"filelinks2","filelinks"}){if(const auto* links=one(attrs,key)){require(links->type==core::Node::Type::List&&links->children.empty(),ErrorCode::Unsupported,"Single-file original linked save location needs explicit reconstruction support");}}}
    const auto directory=platform::pathFromUtf8(saved.saveDirectory);require(saved.saveFile&&directory.is_absolute(),ErrorCode::Unsupported,"Original reset requires supported absolute saved mapping");row.saveLocation=platform::pathToUtf8(directory/platform::pathFromUtf8(*saved.saveFile));row.size=layout->bytes;row.addTime=number(params,"stats.download.added.time");row.completeTime=core::originalDownloadHistoryCompleteTime(number(attrs,"complt"),number(params,"stats.download.completed.time"));
    require(bool(core::editSavedDownloadHistory("de",core::DownloadHistoryAppend{row})),ErrorCode::Unsupported,"Original reset history record exceeds supported format");value->records.push_back(std::move(row));
  }
  require(value->activeCurrent(),ErrorCode::Conflict,"History reset active directory changed");for(const auto& item:value->sources){require(item.matches(capture(lease.directoryHandle(),value->active.get(),item.name)),ErrorCode::Conflict,"History reset source changed during capture");}budget(stop,value->end);return HistoryReconstructionReceipt(std::move(value));
}catch(const Error& error){return std::unexpected(error);}catch(...){return std::unexpected(Error{ErrorCode::Io,"Cannot capture complete authoritative history reset source"});}}
std::expected<void,Error> HistoryReconstructionReceipt::current(const ProfileLease& lease,bool exactEpoch)const{try{
  require(data_&&data_->worker==std::this_thread::get_id()&&data_->lease->sharesOwnership(lease),ErrorCode::Conflict,"Missing/foreign/consumed history reset source");(void)take(lease.retain());require(Clock::now()<data_->end&&(!exactEpoch||data_->epoch==lease.sourceRevision())&&data_->activeCurrent(),ErrorCode::Conflict,"Stale history reset source/deadline/directory");
  for(const auto& item:data_->sources){require(item.matches(capture(lease.directoryHandle(),data_->active.get(),item.name)),ErrorCode::Conflict,"History reset source changed");}return {};
}catch(const Error& error){return std::unexpected(error);}catch(...){return std::unexpected(Error{ErrorCode::Io,"Cannot validate history reset source"});}}
std::span<const core::DownloadHistoryNewRecord> HistoryReconstructionReceipt::records()const{return data_->records;}
} // namespace biglybtc::storage
