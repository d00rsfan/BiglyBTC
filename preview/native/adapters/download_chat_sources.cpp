// SPDX-License-Identifier: GPL-2.0-or-later
#include "download_chat_sources.hpp"
#include <algorithm>
#include <atomic>
#include <cerrno>
#include <fcntl.h>
#include <map>
#include <mutex>
#include <sys/stat.h>
#include <unistd.h>
namespace biglybtc::storage {
namespace {
std::unexpected<Error> bad(ErrorCode code,const char* text){return std::unexpected(Error{code,text});}
struct Descriptor {int value=-1;~Descriptor(){if(value>=0){::close(value);}}};
struct Stamp {bool exists=false;std::array<std::uint64_t,11> fields{};bool operator==(const Stamp&)const=default;};
struct Proof {std::array<std::uint64_t,2> active{};std::array<Stamp,9> files{};bool operator==(const Proof&)const=default;};
std::expected<Stamp,Error> stamp(int directory,const std::string& name){
 struct stat value{};if(::fstatat(directory,name.c_str(),&value,AT_SYMLINK_NOFOLLOW)){if(errno==ENOENT){return Stamp{};}return bad(ErrorCode::Io,"Cannot inspect download chat metadata");}
 if(!S_ISREG(value.st_mode)||value.st_uid!=::geteuid()||value.st_nlink!=1||(value.st_mode&0002)){return bad(ErrorCode::UnsafePath,"Unsafe download chat metadata entry");}
 return Stamp{true,{std::uint64_t(value.st_dev),std::uint64_t(value.st_ino),std::uint64_t(value.st_mode),std::uint64_t(value.st_uid),std::uint64_t(value.st_gid),std::uint64_t(value.st_nlink),std::uint64_t(value.st_size),std::uint64_t(value.st_mtim.tv_sec),std::uint64_t(value.st_mtim.tv_nsec),std::uint64_t(value.st_ctim.tv_sec),std::uint64_t(value.st_ctim.tv_nsec)}};
}
std::expected<Proof,Error> observe(const ProfileLease& lease,const core::TorrentHash& hash){
 Descriptor active{::openat(lease.directoryHandle(),"active",O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW)};if(active.value<0){return bad(ErrorCode::UnsafePath,"Cannot inspect the download chat state directory");}struct stat directory{};if(::fstat(active.value,&directory)||!S_ISDIR(directory.st_mode)||directory.st_uid!=::geteuid()||(directory.st_mode&0002)){return bad(ErrorCode::UnsafePath,"Unsafe download chat state directory");}
 Proof proof;proof.active={std::uint64_t(directory.st_dev),std::uint64_t(directory.st_ino)};const std::array<std::string,6> root{"biglybt.config","biglybt.config.bak","biglybt.config.saving","downloads.config","downloads.config.bak","downloads.config.saving"};std::size_t index=0;
 for(const auto& name:root){auto value=stamp(lease.directoryHandle(),name);if(!value){return std::unexpected(value.error());}proof.files[index++]=*value;}const auto name=hash.hex()+".dat";for(const auto& leaf:std::array<std::string,3>{name,name+".bak","cache.dat"}){auto value=stamp(active.value,leaf);if(!value){return std::unexpected(value.error());}proof.files[index++]=*value;}return proof;
}
}
struct DownloadChatSources::Impl {
 explicit Impl(std::shared_ptr<const ProfileLease> lease):lease(std::move(lease)){}
 struct Entry {DownloadChatSnapshot source;Proof proof;std::uint64_t used;};
 std::shared_ptr<const ProfileLease> lease;std::mutex mutex;std::map<std::string,Entry> entries;
 std::uint64_t clock=0;std::atomic<std::uint64_t> reads{0},hits{0},configReads{0};std::atomic<std::size_t> count{0};
};
DownloadChatSources::DownloadChatSources(std::unique_ptr<Impl> impl):impl_(std::move(impl)){}
DownloadChatSources::~DownloadChatSources()=default;
std::expected<std::shared_ptr<DownloadChatSources>,Error> DownloadChatSources::create(const ProfileLease& lease){auto held=lease.retain();if(!held){return std::unexpected(held.error());}return std::shared_ptr<DownloadChatSources>(new DownloadChatSources(std::make_unique<Impl>(std::move(*held))));}
std::expected<DownloadChatSnapshot,Error> DownloadChatSources::read(const core::TorrentHash& hash){
 try{
  auto access=impl_->lease->sourceAccess();if(!access){return std::unexpected(access.error());}auto pending=requireNoPendingSubscriptionAcquisition(*impl_->lease);if(!pending){return std::unexpected(pending.error());}std::lock_guard lock(impl_->mutex);const auto key=hash.hex();auto proof=observe(*impl_->lease,hash);if(!proof){impl_->entries.erase(key);impl_->count=impl_->entries.size();++impl_->reads;return readDownloadChat(*impl_->lease,hash);}if(++impl_->clock==0){impl_->entries.clear();impl_->count=0;++impl_->clock;}
  auto found=impl_->entries.find(key);if(found!=impl_->entries.end()&&found->second.proof==*proof){found->second.used=impl_->clock;++impl_->hits;return found->second.source;}
  if(found!=impl_->entries.end()&&found->second.proof.active==proof->active&&std::equal(proof->files.begin()+3,proof->files.end(),found->second.proof.files.begin()+3)){
   ++impl_->configReads;auto valid=validateDownloadChatConfig(*impl_->lease);if(!valid){impl_->entries.erase(found);impl_->count=impl_->entries.size();return std::unexpected(valid.error());}auto after=observe(*impl_->lease,hash);if(after){if(*after!=*proof){return bad(ErrorCode::Conflict,"Download chat config changed during validation");}pending=requireNoPendingSubscriptionAcquisition(*impl_->lease);if(!pending){return std::unexpected(pending.error());}found->second.proof=*after;found->second.used=impl_->clock;++impl_->hits;return found->second.source;}
  }
  ++impl_->reads;auto source=readDownloadChat(*impl_->lease,hash);if(!source){impl_->entries.erase(key);impl_->count=impl_->entries.size();return std::unexpected(source.error());}auto after=observe(*impl_->lease,hash);if(!after){impl_->entries.erase(key);impl_->count=impl_->entries.size();return source;}pending=requireNoPendingSubscriptionAcquisition(*impl_->lease);if(!pending){return std::unexpected(pending.error());}if(*after!=*proof){return bad(ErrorCode::Conflict,"Download chat metadata changed during validation");}
  if(impl_->entries.size()>=64&&found==impl_->entries.end()){const auto oldest=std::ranges::min_element(impl_->entries,{},[](const auto& entry){return entry.second.used;});impl_->entries.erase(oldest);}impl_->entries.insert_or_assign(key,Impl::Entry{*source,*proof,impl_->clock});impl_->count=impl_->entries.size();return source;
 }catch(const std::bad_alloc&){return bad(ErrorCode::Io,"Insufficient memory for download chat subjects");}
}
DownloadChatSourceStatistics DownloadChatSources::statistics()const noexcept{return {impl_->reads.load(std::memory_order_relaxed),impl_->hits.load(std::memory_order_relaxed),impl_->count.load(std::memory_order_relaxed),impl_->configReads.load(std::memory_order_relaxed)};}
}
