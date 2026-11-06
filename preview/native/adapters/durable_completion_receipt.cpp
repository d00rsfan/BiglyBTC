// SPDX-License-Identifier: GPL-2.0-or-later
#include "durable_completion_receipt.hpp"
#include "encoded_document.hpp"
#include "file_io.hpp"
#include "original_torrent_identity.hpp"
#include <openssl/evp.h>
#include "biglybtc/state_cache.hpp"
#include <array>
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <thread>
#include <algorithm>
#include <limits>
#include <unistd.h>

namespace biglybtc::storage {
namespace {
using detail::Descriptor;using Clock=std::chrono::steady_clock;
void require(bool ok,ErrorCode code,const char* message){if(!ok){throw Error{code,message};}}
template<class T>T take(std::expected<T,Error> v){if(!v){throw v.error();}return std::move(*v);}
bool same(const struct stat& a,const struct stat& b){return a.st_dev==b.st_dev&&a.st_ino==b.st_ino&&a.st_mode==b.st_mode&&a.st_uid==b.st_uid&&a.st_gid==b.st_gid&&a.st_nlink==b.st_nlink&&a.st_size==b.st_size&&a.st_mtim.tv_sec==b.st_mtim.tv_sec&&a.st_mtim.tv_nsec==b.st_mtim.tv_nsec&&a.st_ctim.tv_sec==b.st_ctim.tv_sec&&a.st_ctim.tv_nsec==b.st_ctim.tv_nsec;}
struct Source {std::string name;Descriptor fd{-1};std::optional<struct stat> stat;std::optional<std::string> bytes;};
Descriptor directory(int root,std::string_view name){Descriptor fd(::openat(root,std::string(name).c_str(),O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC));require(fd.get()>=0,ErrorCode::UnsafePath,"Committed completion directory unavailable");return fd;}
Source capture(int root,std::string name){
  Source out;out.name=std::move(name);const auto slash=out.name.find('/');std::optional<Descriptor> sub;int parent=root;std::string leaf=out.name;
  if(slash!=std::string::npos){sub.emplace(directory(root,std::string_view(out.name).substr(0,slash)));parent=sub->get();leaf=out.name.substr(slash+1);}
  struct stat before{};if(::fstatat(parent,leaf.c_str(),&before,AT_SYMLINK_NOFOLLOW)!=0){require(errno==ENOENT,ErrorCode::Io,"Committed completion source stat failed");return out;}
  require(S_ISREG(before.st_mode)&&before.st_uid==::geteuid()&&before.st_nlink==1,ErrorCode::UnsafePath,"Committed completion source must be owned single-link file");
  out.fd=Descriptor(::openat(parent,leaf.c_str(),O_RDONLY|O_NONBLOCK|O_NOFOLLOW|O_CLOEXEC));struct stat opened{},after{};
  require(out.fd.get()>=0&&::fstat(out.fd.get(),&opened)==0&&same(before,opened),ErrorCode::Conflict,"Committed completion source replaced");
  out.bytes=take(detail::readFile(parent,leaf,64U*1024U*1024U));out.stat=opened;
  require(out.bytes&&::fstat(out.fd.get(),&after)==0&&same(opened,after)&&::fstatat(parent,leaf.c_str(),&after,AT_SYMLINK_NOFOLLOW)==0&&same(opened,after),ErrorCode::Conflict,"Committed completion source changed during capture");return out;
}
bool equal(const Source& a,const Source& b){struct stat held{};return (!a.stat||(::fstat(a.fd.get(),&held)==0&&same(*a.stat,held)))&&bool(a.stat)==bool(b.stat)&&(!a.stat||same(*a.stat,*b.stat))&&a.bytes==b.bytes;}
core::Document document(const std::string& bytes){core::Limits limits;limits.inputBytes=64U*1024U*1024U;auto value=EncodedDocument::parse(bytes,limits);require(bool(value)&&value->document().root().type==core::Node::Type::Dictionary,ErrorCode::InvalidFormat,"Invalid committed completion document");return value->document();}
std::int64_t number(const core::Node* map,std::string_view key,std::int64_t absent=0){if(!map){return absent;}require(map->type==core::Node::Type::Dictionary,ErrorCode::InvalidFormat,"Invalid committed completion attributes");const auto* value=map->find(key);if(!value){return absent;}require(value->type==core::Node::Type::Integer,ErrorCode::InvalidFormat,"Invalid original history numeric input");return value->integer;}
std::string raw(const auto& value){return {reinterpret_cast<const char*>(value.data()),value.size()};}
}
struct DurableCompletionReceipt::Data {
  std::shared_ptr<const ProfileLease> lease;std::thread::id worker=std::this_thread::get_id();
  std::array<std::uint64_t,3> owner{};std::uint64_t admission=0,epoch=0;Clock::time_point end;
  std::vector<Source> sources;core::DownloadHistoryNewRecord record;bool monitored=false;
};
DurableCompletionReceipt::DurableCompletionReceipt(std::unique_ptr<Data> data):data_(std::move(data)){}
DurableCompletionReceipt::DurableCompletionReceipt(DurableCompletionReceipt&&)noexcept=default;
DurableCompletionReceipt& DurableCompletionReceipt::operator=(DurableCompletionReceipt&&)noexcept=default;
DurableCompletionReceipt::~DurableCompletionReceipt()=default;
std::expected<DurableCompletionReceipt,Error> DurableCompletionReceipt::issue(const ProfileLease& lease,
    const OriginalTorrentIdentity& expected,const core::SavedDownload& captured,
    std::array<std::uint64_t,3> owner,std::uint64_t admission,std::string_view acceptedState,
    std::int64_t eventMillis,Transition transition,std::int64_t verifiedBytes,std::int64_t received,std::int64_t sent,std::int64_t payloadBytes){try{
  require(admission&&std::ranges::all_of(owner,[](auto n){return n!=0;})&&eventMillis>0&&payloadBytes>0
      &&verifiedBytes>=0&&verifiedBytes<=payloadBytes
      &&(transition==Transition::Complete?verifiedBytes==payloadBytes:verifiedBytes<payloadBytes),
      ErrorCode::Conflict,"Completion requires an actual nonzero owner/admission/event");
  auto access=take(lease.sourceAccess());auto value=std::make_unique<Data>();value->lease=take(lease.retain());value->owner=owner;value->admission=admission;value->epoch=lease.sourceRevision();value->end=Clock::now()+std::chrono::seconds(30);
  std::size_t total=0;
  const auto add=[&](std::string name)->std::size_t{auto item=capture(lease.directoryHandle(),std::move(name));const auto size=item.bytes?item.bytes->size():0;require(size<=128U*1024U*1024U-total,ErrorCode::Unsupported,"Completion receipt source bound");total+=size;value->sources.push_back(std::move(item));return value->sources.size()-1;};
  const auto& hash=expected.originalKey;const auto inventory=add("downloads.config"),state=add("active/"+hash.hex()+".dat");
  for(const auto* name:{"downloads.config.bak","downloads.config.saving","biglybt.config","biglybt.config.bak","biglybt.config.saving","active/cache.dat","active/cache.dat.bak","active/cache.dat.saving"}){add(name);}
  add("active/"+hash.hex()+".dat.bak");
  for(const auto* suffix:{".dat.saving",".dat.import"}){require(!value->sources[add("active/"+hash.hex()+suffix)].bytes,ErrorCode::Conflict,"Completion state has a pending candidate");}
  for(const auto* name:{"BiglyBTC.torrent-import","BiglyBTC.torrent-import.saving","BiglyBTC.torrent-import.inventory","BiglyBTC.torrent-import.backup"}){require(!value->sources[add(name)].bytes,ErrorCode::Conflict,"Completion overlaps pending import");}
  require(value->sources[inventory].bytes&&value->sources[state].bytes,ErrorCode::Missing,"Completed checkpoint sources absent");
  auto original=document(*value->sources[state].bytes);require(original.serialize()==acceptedState,ErrorCode::Conflict,"Actual checkpoint state changed before completion consumption");
  auto list=document(*value->sources[inventory].bytes);auto saved=core::readLibrary(list);require(saved&&saved->issues.empty(),ErrorCode::InvalidFormat,"Ambiguous completion inventory");
  const core::SavedDownload* selected=nullptr;for(const auto& row:saved->downloads){if(row.hash&&*row.hash==hash){require(!selected,ErrorCode::Conflict,"Duplicate completed target");selected=&row;}}require(selected,ErrorCode::Missing,"Completed target no longer saved");
  // Position/start/pause changes and unrelated rows are not history completion
  // inputs. THIS actual checkpoint's mapping and accepted payload prefix are.
  const auto previousReceived=captured.downloaded.value_or(0),previousSent=captured.uploaded.value_or(0);
  require(received>=0&&sent>=0&&previousReceived>=0&&previousSent>=0&&received<=std::numeric_limits<std::int64_t>::max()-previousReceived&&sent<=std::numeric_limits<std::int64_t>::max()-previousSent,
      ErrorCode::Conflict,"Invalid accepted completion payload prefix");
  require(selected->torrentFile==captured.torrentFile&&selected->saveDirectory==captured.saveDirectory&&selected->saveFile==captured.saveFile&&selected->legacySavePath==captured.legacySavePath&&selected->persistent==captured.persistent
      &&selected->downloaded.value_or(0)==previousReceived+received&&selected->uploaded.value_or(0)==previousSent+sent&&selected->completedBytes==verifiedBytes,
      ErrorCode::Conflict,"Completed mapping/counter postimage changed");
  const Source* cache=nullptr;const Source* cacheBackup=nullptr;
  for(const auto& item:value->sources){if(item.name=="active/cache.dat"){cache=&item;}if(item.name=="active/cache.dat.bak"){cacheBackup=&item;}}
  require(cache&&cacheBackup&&(cache->bytes||!cacheBackup->bytes),ErrorCode::Conflict,"Completion cache primary unavailable");
  if(cache->bytes){auto cached=document(*cache->bytes);auto projection=core::readStateCache(cached);require(projection&&projection->issues.empty()&&!projection->torrents.contains(hash.hex()),ErrorCode::Conflict,"Completed target retains conflicting cache authority");}
  const auto info=take(originalTorrentInfoSubject(original));OriginalFullInfoHashes full;const auto* node=original.find("info");
  if(node&&node->find("pieces")){full.v1.emplace();unsigned n=0;require(EVP_Digest(info.data(),info.size(),reinterpret_cast<unsigned char*>(full.v1->data()),&n,EVP_sha1(),nullptr)==1&&n==full.v1->size(),ErrorCode::Io,"Completion v1 digest failed");}
  if(const auto* version=node?node->find("meta version"):nullptr;version&&version->type==core::Node::Type::Integer&&version->integer==2){full.v2.emplace();unsigned n=0;require(EVP_Digest(info.data(),info.size(),reinterpret_cast<unsigned char*>(full.v2->data()),&n,EVP_sha256(),nullptr)==1&&n==full.v2->size(),ErrorCode::Io,"Completion v2 digest failed");}
  auto identity=take(selectOriginalTorrentIdentity(original,{info,std::move(full)}));require(identity.originalKey==hash&&identity.full==expected.full&&identity.selected==expected.selected,ErrorCode::Conflict,"Completed full identity differs from actual owner");
  const auto* attrs=original.find("attributes");const auto* params=attrs?attrs->find("parameters"):nullptr;
  value->monitored=core::originalDownloadHistoryMonitored(selected->persistent,static_cast<std::uint64_t>(number(attrs,"flags")));
  auto& row=value->record;row.uid=core::originalDownloadHistoryUid(raw(hash.bytes()),number(attrs,"stats.download.added.time"));
  row.hash=identity.full.v1?std::optional<std::string>{raw(*identity.full.v1)}:std::optional<std::string>{raw(hash.bytes())};if(identity.full.v2){row.hashV2=raw(*identity.full.v2);}
  row.addTime=number(params,"stats.download.added.time");row.completeTime=core::originalDownloadHistoryCompleteTime(number(attrs,"complt"),number(params,"stats.download.completed.time"));
  require(row.completeTime==(transition==Transition::Complete?eventMillis:0),ErrorCode::Conflict,"Actual checkpoint did not preserve its observed completion transition");
  for(const auto& item:value->sources){require(equal(item,capture(lease.directoryHandle(),item.name)),ErrorCode::Conflict,"Completion sources changed while issuing receipt");}
  return DurableCompletionReceipt(std::move(value));
}catch(const Error& e){return std::unexpected(e);}catch(...){return std::unexpected(Error{ErrorCode::Io,"Checkpoint remains saved; completion history proof unavailable"});}}
std::expected<void,Error> DurableCompletionReceipt::current(const ProfileLease& lease,bool exactEpoch)const{try{
  require(data_&&data_->worker==std::this_thread::get_id(),ErrorCode::Conflict,"Missing/consumed/foreign-worker completion receipt");
  require(data_->lease->sharesOwnership(lease),ErrorCode::Conflict,"Foreign completion history OFD");(void)take(lease.retain());
  require(Clock::now()<data_->end&&(!exactEpoch||lease.sourceRevision()==data_->epoch),ErrorCode::Conflict,"Stale completion receipt source/deadline");
  for(const auto& item:data_->sources){require(equal(item,capture(lease.directoryHandle(),item.name)),ErrorCode::Conflict,"Completion source changed after issuance");}return {};
}catch(const Error& e){return std::unexpected(e);}catch(...){return std::unexpected(Error{ErrorCode::Io,"Cannot validate completion source"});}}
const core::DownloadHistoryNewRecord& DurableCompletionReceipt::record()const{return data_->record;}
bool DurableCompletionReceipt::monitored()const{return data_->monitored;}
} // namespace biglybtc::storage
