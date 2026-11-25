// SPDX-License-Identifier: GPL-2.0-or-later
#include "state_file_io.hpp"
#include "original_payload_io.hpp"
#include "durable_removal_receipt.hpp"
#include "encoded_document.hpp"
#include "original_torrent_identity.hpp"
#include "biglybtc/state_cache.hpp"
#include <openssl/evp.h>
#include <algorithm>
#include <array>
#include <new>
#include <thread>
#include <vector>

namespace biglybtc::storage {
namespace {
using detail::StateDescriptor;using detail::StateHandle;using detail::StateStamp;
using detail::stateStamp;using detail::stateStampAt;using detail::sameStateSnapshot;
using Clock=std::chrono::steady_clock;
void require(bool ok,ErrorCode code,const char* message){if(!ok){throw Error{code,message};}}
template<class T>T take(std::expected<T,Error> value){if(!value){throw value.error();}return std::move(*value);}
void take(std::expected<void,Error> value){if(!value){throw value.error();}}
std::array<std::uint64_t,11> metadata(const StateStamp& s){return {static_cast<std::uint64_t>(s.device),static_cast<std::uint64_t>(s.inode),static_cast<std::uint64_t>(s.mode),static_cast<std::uint64_t>(s.owner),static_cast<std::uint64_t>(s.group),static_cast<std::uint64_t>(s.links),static_cast<std::uint64_t>(s.size),static_cast<std::uint64_t>(s.modified.tv_sec),static_cast<std::uint64_t>(s.modified.tv_nsec),static_cast<std::uint64_t>(s.changed.tv_sec),static_cast<std::uint64_t>(s.changed.tv_nsec)};}
struct Source {std::string name;StateDescriptor fd;std::optional<StateStamp> stamp;std::optional<std::string> bytes;};
Source capture(StateHandle root,std::string name){
  Source out;out.name=std::move(name);
  const auto slash=out.name.find('/');
  StateDescriptor directory;
  auto parent=root;std::string leaf=out.name;
  if(slash!=std::string::npos){
    require(out.name.substr(0,slash)=="active",ErrorCode::UnsafePath,"Invalid fixed removal source");
    directory=take(detail::openDirectoryAt(root,"active"));parent=directory.get();leaf=out.name.substr(slash+1);
  }
  const auto named=take(stateStampAt(parent,leaf));
  if(!named){return out;}
  require(named->regular&&named->owned&&named->links==1&&named->size>=0&&named->size<=64*1024*1024,
      ErrorCode::UnsafePath,"Removal source must be owned bounded regular single-link metadata");
  auto payload=take(detail::openOriginalPayloadAt(parent,leaf,detail::OriginalPayloadAccess::ReadOnly,1,true));
  out.fd=StateDescriptor(payload.release());
  const auto held=take(stateStamp(out.fd.get()));
  require(sameStateSnapshot(*named,held),ErrorCode::Conflict,"Removal source changed before open");
  out.bytes.emplace(static_cast<std::size_t>(held.size),'\0');
  const auto bytes=std::as_writable_bytes(std::span(*out.bytes));
  for(std::size_t offset=0;offset<bytes.size();){
    const auto count=detail::readOriginalPayload(out.fd.get(),offset,bytes.subspan(offset));
    require(count>0,ErrorCode::Conflict,"Removal source changed during read");offset+=static_cast<std::size_t>(count);
  }
  const auto after=take(stateStampAt(parent,leaf));
  require(after&&sameStateSnapshot(held,*after)&&sameStateSnapshot(held,take(stateStamp(out.fd.get()))),
      ErrorCode::Conflict,"Removal source changed during capture");
  out.stamp=held;return out;
}
bool equal(const Source& a,const Source& b){
  return bool(a.stamp)==bool(b.stamp)&&a.bytes==b.bytes
      &&(!a.stamp||(sameStateSnapshot(*a.stamp,take(stateStamp(a.fd.get())))&&sameStateSnapshot(*a.stamp,*b.stamp)));
}
bool sameActive(StateHandle root,StateHandle active,const StateStamp& expected){
  const auto named=take(stateStampAt(root,"active"));
  return named&&sameStateSnapshot(*named,expected)&&sameStateSnapshot(take(stateStamp(active)),expected);
}
core::Document document(const std::string& bytes){auto value=EncodedDocument::parse(bytes,compatibleConfigLimits());require(bool(value)&&value->document().root().type==core::Node::Type::Dictionary,ErrorCode::InvalidFormat,"Invalid original removal metadata");return value->document();}
std::int64_t number(const core::Node* map,std::string_view key){if(!map){return 0;}require(map->type==core::Node::Type::Dictionary,ErrorCode::InvalidFormat,"Malformed original removal attributes");const auto* value=map->find(key);if(!value){return 0;}require(value->type==core::Node::Type::Integer,ErrorCode::InvalidFormat,"Malformed original removal numeric input");return value->integer;}
std::string raw(const auto& bytes){return {reinterpret_cast<const char*>(bytes.data()),bytes.size()};}
}
struct DurableRemovalReceipt::Data {
  std::shared_ptr<const ProfileLease> lease;std::thread::id worker=std::this_thread::get_id();
  std::uint64_t epoch=0;Clock::time_point end;std::vector<Source> sources;core::TorrentHash selected;
  core::TorrentHash originalKey;
  StateDescriptor active;StateStamp activeStamp{};
  core::DownloadHistoryNewRecord record;bool monitored=false;
  std::optional<CapturedRemovalTags> tags;std::optional<Error> tagWarning;
};
DurableRemovalReceipt::DurableRemovalReceipt(std::unique_ptr<Data> data):data_(std::move(data)){}
DurableRemovalReceipt::DurableRemovalReceipt(DurableRemovalReceipt&&)noexcept=default;
DurableRemovalReceipt& DurableRemovalReceipt::operator=(DurableRemovalReceipt&&)noexcept=default;
DurableRemovalReceipt::~DurableRemovalReceipt()=default;
std::expected<DurableRemovalReceipt,Error> DurableRemovalReceipt::issue(const ProfileLease& lease,
    const core::SavedDownload& captured,std::string_view acceptedBeforeState,
    std::span<const Postimage> committed,std::array<std::uint64_t,2> activeIdentity,
    std::int64_t eventMillis,Clock::time_point eventEnd,CapturedRemovalTags* tags){try{
  require(captured.hash&&captured.persistent,ErrorCode::Conflict,"Removal needs the actual persistent prepared target");const auto& hash=*captured.hash;
  auto access=take(lease.sourceAccess());auto value=std::make_unique<Data>();value->lease=take(lease.retain());value->epoch=lease.sourceRevision();value->selected=hash;value->end=eventEnd;require(eventMillis>0&&Clock::now()<eventEnd,ErrorCode::Conflict,"Actual removal event expired");
  value->active=take(detail::openDirectoryAt(lease.directoryHandle(),"active"));
  value->activeStamp=take(stateStamp(value->active.get()));
  require(value->activeStamp.owned&&value->activeStamp.links&&value->activeStamp.device==activeIdentity[0]
      &&value->activeStamp.inode==activeIdentity[1],ErrorCode::Conflict,"Actual prepared active directory changed during removal");
  require(!committed.empty(),ErrorCode::Conflict,"Fresh removal publication proof absent");
  for(const auto& expected:committed){const auto now=capture(lease.directoryHandle(),expected.name);require(now.bytes==expected.bytes&&bool(now.stamp)==bool(expected.metadata)&&(!now.stamp||metadata(*now.stamp)==*expected.metadata),ErrorCode::Conflict,"Fresh removal committed postimage changed before issuance");}

  // This method is callable ONLY from the actual fresh apply success branch.
  // Recovered journals have no issuer. Postimages are checked independently of
  // that control-flow proof, and remain current through history publication.
  std::size_t bytes=0;
  const auto add=[&](std::string name)->std::size_t{auto item=capture(lease.directoryHandle(),std::move(name));const auto expected=std::find_if(committed.begin(),committed.end(),[&](const auto& proof){return proof.name==item.name;});if(expected!=committed.end()){require(item.bytes==expected->bytes&&bool(item.stamp)==bool(expected->metadata)&&(!item.stamp||metadata(*item.stamp)==*expected->metadata),ErrorCode::Conflict,"Removal captured postimage differs from actual publication");}const auto size=item.bytes?item.bytes->size():0;require(size<=128U*1024U*1024U-bytes,ErrorCode::Unsupported,"Removal witness metadata bound");bytes+=size;value->sources.push_back(std::move(item));return value->sources.size()-1;};
  for(const auto* name:{"downloads.config","downloads.config.bak"}){
    const auto index=add(name);const auto& item=value->sources[index];if(!item.bytes){require(std::string_view(name)!="downloads.config",ErrorCode::Missing,"Removed inventory primary disappeared");continue;}
    const auto doc=document(*item.bytes);const auto rows=core::readLibrary(doc);require(rows&&rows->issues.empty(),ErrorCode::InvalidFormat,"Removed inventory is ambiguous");
    for(const auto& row:rows->downloads){require(!row.hash||*row.hash!=hash,ErrorCode::Conflict,"Removed target remains in inventory");}
    for(const auto& row:rows->paused){require(row.hash!=hash,ErrorCode::Conflict,"Removed target retains pause intent");}
  }
  for(const auto* name:{"active/cache.dat","active/cache.dat.bak"}){
    const auto index=add(name);const auto& item=value->sources[index];if(!item.bytes){continue;}const auto doc=document(*item.bytes);const auto rows=core::readStateCache(doc);require(rows&&rows->issues.empty()&&!rows->torrents.contains(hash.hex()),ErrorCode::Conflict,"Removed target retains state-cache authority");
  }
  const auto primary=std::find_if(value->sources.begin(),value->sources.end(),[](const auto& item){return item.name=="active/cache.dat";});const auto backup=std::find_if(value->sources.begin(),value->sources.end(),[](const auto& item){return item.name=="active/cache.dat.bak";});
  require(primary!=value->sources.end()&&backup!=value->sources.end()&&(primary->bytes||!backup->bytes),ErrorCode::Conflict,"Removal cache candidates became ambiguous");
  for(const auto* suffix:{".dat",".dat.bak",".dat.saving",".dat.bak.saving",".dat.import"}){require(!value->sources[add("active/"+hash.hex()+suffix)].bytes,ErrorCode::Conflict,"Removed target retains active state or candidate");}
  for(const auto* name:{"downloads.config.saving","downloads.config.bak.saving","active/cache.dat.saving","active/cache.dat.bak.saving","BiglyBTC.file-operation","BiglyBTC.file-operation.BiglyBTC.fileop-saving","BiglyBTC.torrent-import","BiglyBTC.torrent-import.saving","BiglyBTC.torrent-import.inventory","BiglyBTC.torrent-import.backup"}){require(!value->sources[add(name)].bytes,ErrorCode::Conflict,"Removal journal/candidate cleanup is not complete");}
  for(const auto* name:{"biglybt.config","biglybt.config.bak","biglybt.config.saving"}){add(name);}
  auto original=document(std::string(acceptedBeforeState));const auto info=take(originalTorrentInfoSubject(original));const auto* node=original.find("info");OriginalFullInfoHashes full;
  const auto digest=[&](const EVP_MD* algorithm,auto& result){unsigned count=0;require(EVP_Digest(info.data(),info.size(),reinterpret_cast<unsigned char*>(result.data()),&count,algorithm,nullptr)==1&&count==result.size(),ErrorCode::Io,"Removal full identity digest failed");};
  if(node&&node->find("pieces")){full.v1.emplace();digest(EVP_sha1(),*full.v1);}if(node&&node->find("file tree")){full.v2.emplace();digest(EVP_sha256(),*full.v2);}
  const auto identity=take(selectOriginalTorrentIdentity(original,{info,std::move(full)}));take(validateOriginalTorrentKey(identity,hash,true));
  value->originalKey=identity.originalKey;
  if(hash.bytes().size()==32){
    // Mapping a Native runtime key is not authority to merge original managers.
    const auto inventory=std::find_if(value->sources.begin(),value->sources.end(),[](const auto& item){return item.name=="downloads.config";});require(inventory!=value->sources.end()&&inventory->bytes,ErrorCode::Conflict,"Full32 history mapping needs committed inventory");
    const auto remaining=core::readLibrary(document(*inventory->bytes),{10000,10000});require(remaining&&remaining->issues.empty(),ErrorCode::Conflict,"Full32 history inventory mapping is incomplete");
    for(const auto& row:remaining->downloads){require(row.hash&&row.hash->bytes().size()==20&&*row.hash!=identity.originalKey,ErrorCode::Conflict,"Another owner aliases or lacks a proven original selected identity");}
  }
  const auto* attrs=original.find("attributes");const auto* parameters=attrs?attrs->find("parameters"):nullptr;
  value->monitored=core::originalDownloadHistoryMonitored(captured.persistent,static_cast<std::uint64_t>(number(attrs,"flags")));
  auto& record=value->record;record.uid=core::originalDownloadHistoryUid(raw(identity.originalKey.bytes()),number(attrs,"stats.download.added.time"));
  record.hash=identity.full.v1?std::optional<std::string>{raw(*identity.full.v1)}:std::optional<std::string>{raw(identity.originalKey.bytes())};if(identity.full.v2){record.hashV2=raw(*identity.full.v2);}
  record.addTime=number(parameters,"stats.download.added.time");record.removeTime=eventMillis;
  if(tags){auto current=tags->current(lease,hash);if(current){current=tags->matchesIdentity(identity);}if(current){record.tags=tags->names();value->tags.emplace(std::move(*tags));}else{value->tagWarning=current.error();}}

  for(const auto& proof:committed){require(std::any_of(value->sources.begin(),value->sources.end(),[&](const auto& item){return item.name==proof.name;}),ErrorCode::Conflict,"Uncaptured committed removal source");}
  for(const auto& item:value->sources){require(equal(item,capture(lease.directoryHandle(),item.name)),ErrorCode::Conflict,"Removal sources changed while issuing witness");}
  require(Clock::now()<eventEnd,ErrorCode::Conflict,"Removal witness issuance exceeded actual event lifetime");
  require(sameActive(lease.directoryHandle(),value->active.get(),value->activeStamp),ErrorCode::Conflict,"Removal active directory changed during issuance");
  return DurableRemovalReceipt(std::move(value));
}catch(const Error& error){return std::unexpected(error);}catch(...){return std::unexpected(Error{ErrorCode::Io,"Removal remains committed; history witness unavailable"});}}
std::expected<void,Error> DurableRemovalReceipt::current(const ProfileLease& lease,bool exactEpoch)const{try{
  require(data_&&data_->worker==std::this_thread::get_id()&&data_->lease->sharesOwnership(lease),ErrorCode::Conflict,"Consumed/foreign-worker/OFD removal witness");(void)take(lease.retain());
  require(Clock::now()<data_->end&&(!exactEpoch||lease.sourceRevision()==data_->epoch),ErrorCode::Conflict,"Removal witness source/deadline changed");
  require(sameActive(lease.directoryHandle(),data_->active.get(),data_->activeStamp),ErrorCode::Conflict,"Removal active directory changed before history consumption");
  if(data_->tags){take(data_->tags->current(lease,data_->selected));}
  for(const auto& item:data_->sources){require(equal(item,capture(lease.directoryHandle(),item.name)),ErrorCode::Conflict,"Removal postimage changed before history consumption");}
  require(sameActive(lease.directoryHandle(),data_->active.get(),data_->activeStamp)&&Clock::now()<data_->end,ErrorCode::Conflict,"Removal directory/lifetime changed during validation");return {};
}catch(const Error& error){return std::unexpected(error);}catch(...){return std::unexpected(Error{ErrorCode::Io,"Cannot validate removal witness"});}}
std::expected<core::TorrentHash,Error> DurableRemovalReceipt::removedTagIdentity(
    const ProfileLease& lease, const core::TorrentHash& runtime) const {
  try {
    require(data_ && data_->selected == runtime && data_->originalKey.bytes().size() == 20,
        ErrorCode::Conflict,"Removal receipt has another original taggable");
    take(current(lease,true));
    if (runtime != data_->originalKey) {
      for (const auto& source : data_->sources) {
        if (!source.bytes || (source.name != "active/cache.dat" && source.name != "active/cache.dat.bak")) { continue; }
        const auto rows = core::readStateCache(document(*source.bytes));
        require(rows && rows->issues.empty() && !rows->torrents.contains(data_->originalKey.hex()),
            ErrorCode::Conflict,"Removed original taggable still has an independent cache entry");
      }
    }
    return data_->originalKey;
  } catch (const Error& error) {
    return std::unexpected(error);
  } catch (const std::bad_alloc&) {
    return std::unexpected(Error{ErrorCode::Io,"Cannot verify the removed original taggable"});
  }
}
const core::DownloadHistoryNewRecord& DurableRemovalReceipt::record()const{return data_->record;}
bool DurableRemovalReceipt::monitored()const{return data_->monitored;}
const std::optional<Error>& DurableRemovalReceipt::tagWarning()const{return data_->tagWarning;}
} // namespace biglybtc::storage
