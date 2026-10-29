// SPDX-License-Identifier: GPL-2.0-or-later
#include "azureus_value_versions.hpp"
#include "file_io.hpp"
#include <array>
#include <cerrno>
#include <fcntl.h>
#include <mutex>
#include <set>
#include <sys/stat.h>
#include <unistd.h>
namespace biglybtc::storage {
namespace {
using detail::Descriptor;
constexpr core::Limits Limits{.inputBytes=65536,.stringBytes=65536,.nodes=2048,.depth=32};
std::unexpected<Error> error(ErrorCode c,const char* text){return std::unexpected(Error{c,text});}
bool same(const struct stat& a,const struct stat& b){return a.st_dev==b.st_dev&&a.st_ino==b.st_ino&&a.st_mode==b.st_mode&&a.st_uid==b.st_uid&&a.st_nlink==b.st_nlink&&a.st_size==b.st_size&&a.st_mtim.tv_sec==b.st_mtim.tv_sec&&a.st_mtim.tv_nsec==b.st_mtim.tv_nsec&&a.st_ctim.tv_sec==b.st_ctim.tv_sec&&a.st_ctim.tv_nsec==b.st_ctim.tv_nsec;}
struct Capture {std::optional<std::string> bytes;std::optional<struct stat> stamp;};
std::expected<Capture,Error> capture(int dir,const char* name){
  Capture out;struct stat value{};
  if(::fstatat(dir,name,&value,AT_SYMLINK_NOFOLLOW)==0){
    if(!S_ISREG(value.st_mode)||value.st_uid!=::geteuid()||value.st_nlink!=1||value.st_size<0||value.st_size>static_cast<off_t>(Limits.inputBytes)){return error(ErrorCode::UnsafePath,"Unsafe Azureus version input");}out.stamp=value;
  }else if(errno!=ENOENT){return error(ErrorCode::Io,"Cannot inspect Azureus version input");}
  auto read=detail::readFile(dir,name,Limits.inputBytes);if(!read){return std::unexpected(read.error());}out.bytes=std::move(*read);
  struct stat after{};const bool present=::fstatat(dir,name,&after,AT_SYMLINK_NOFOLLOW)==0;
  if(!present&&errno!=ENOENT){return error(ErrorCode::Io,"Cannot recheck Azureus version input");}
  if(present!=out.stamp.has_value()||present!=out.bytes.has_value()||(present&&!same(*out.stamp,after))){return error(ErrorCode::Conflict,"Azureus version input changed while reading");}return out;
}
bool equal(const Capture& a,const Capture& b){return a.bytes==b.bytes&&a.stamp.has_value()==b.stamp.has_value()&&(!a.stamp||same(*a.stamp,*b.stamp));}
std::expected<core::Document,Error> parse(std::string_view bytes){
  auto doc=core::Document::parse(std::string(bytes),Limits);if(!doc||doc->root().type!=core::Node::Type::Dictionary){return error(ErrorCode::InvalidFormat,"Invalid Azureus version map");}
  std::set<std::string_view> keys;for(std::size_t i=0;i<doc->root().children.size();i+=2){if(!keys.insert(doc->root().children[i].bytes).second){return error(ErrorCode::InvalidFormat,"Duplicate Azureus version key");}}
  if(const auto* next=doc->find("next");next&&next->type!=core::Node::Type::Integer){return error(ErrorCode::InvalidFormat,"Invalid Azureus next version type");}return std::move(*doc);
}
std::expected<Descriptor,Error> directory(int parent,const std::string& name){
  Descriptor fd(::openat(parent,name.c_str(),O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW));
  if(fd.get()<0&&errno==ENOENT){if(::mkdirat(parent,name.c_str(),0700)!=0&&errno!=EEXIST){return error(ErrorCode::Io,"Cannot create Azureus version directory");}if(::fsync(parent)!=0){return error(ErrorCode::Io,"Cannot sync Azureus version parent");}fd=Descriptor(::openat(parent,name.c_str(),O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW));}
  struct stat held{},named{};if(fd.get()<0||::fstat(fd.get(),&held)!=0||::fstatat(parent,name.c_str(),&named,AT_SYMLINK_NOFOLLOW)!=0||!S_ISDIR(held.st_mode)||!S_ISDIR(named.st_mode)||held.st_uid!=::geteuid()||held.st_dev!=named.st_dev||held.st_ino!=named.st_ino){return error(ErrorCode::UnsafePath,"Unsafe Azureus version directory");}return fd;
}
bool namedDirectory(int parent,const std::string& name,int held){struct stat a{},b{};return ::fstat(held,&a)==0&&::fstatat(parent,name.c_str(),&b,AT_SYMLINK_NOFOLLOW)==0&&S_ISDIR(a.st_mode)&&S_ISDIR(b.st_mode)&&a.st_uid==::geteuid()&&a.st_dev==b.st_dev&&a.st_ino==b.st_ino;}
}
struct AzureusValueVersions::Impl {
  std::shared_ptr<const ProfileLease> lease;Descriptor dht{-1},net{-1};std::int32_t network=0;std::string netName;
  Capture primary;std::uint32_t next=0;unsigned remaining=0;bool poisoned=false;mutable std::mutex mutex;
  int dir()const{return network?net.get():dht.get();}
  std::expected<void,Error> validate()const {
    if(poisoned){return error(ErrorCode::Conflict,"Azureus version reservation requires explicit recovery");}
    auto current=lease->retain();if(!current){return std::unexpected(current.error());}
    if(!namedDirectory(lease->directoryHandle(),"dht",dht.get())||(network&&!namedDirectory(dht.get(),netName,net.get()))){return error(ErrorCode::Conflict,"Azureus version directory changed");}
    auto p=capture(dir(),"version.dat"),pending=capture(dir(),"version.saving");if(!p){return std::unexpected(p.error());}if(!pending){return std::unexpected(pending.error());}
    if(!equal(*p,primary)||pending->bytes){return error(ErrorCode::Conflict,"Azureus version source or pending publication changed");}return {};
  }
};
AzureusValueVersions::AzureusValueVersions(std::unique_ptr<Impl> value):impl_(std::move(value)){}
AzureusValueVersions::~AzureusValueVersions()=default;
std::int32_t AzureusValueVersions::network()const noexcept{return impl_->network;}
std::expected<std::shared_ptr<AzureusValueVersions>,Error> AzureusValueVersions::open(const ProfileLease& source,std::int32_t network){
  try{
    if(network<0){return error(ErrorCode::InvalidFormat,"Invalid Azureus network");}auto held=source.retain();if(!held){return std::unexpected(held.error());}
    auto out=std::make_unique<Impl>();out->lease=std::move(*held);out->network=network;auto dht=directory(out->lease->directoryHandle(),"dht");if(!dht){return std::unexpected(dht.error());}out->dht=std::move(*dht);
    if(network){out->netName="net"+std::to_string(network);auto net=directory(out->dht.get(),out->netName);if(!net){return std::unexpected(net.error());}out->net=std::move(*net);}
    auto primary=capture(out->dir(),"version.dat"),pending=capture(out->dir(),"version.saving");if(!primary){return std::unexpected(primary.error());}if(!pending){return std::unexpected(pending.error());}
    if(primary->bytes){auto doc=parse(*primary->bytes);if(!doc){return std::unexpected(doc.error());}}
    if(pending->bytes){
      // Original reads .saving ONLY when .dat is absent. Recover precisely that
      // complete source; never delete an uncertain companion of a live primary.
      if(primary->bytes){return error(ErrorCode::Conflict,"Review Azureus version pending publication");}auto doc=parse(*pending->bytes);if(!doc){return std::unexpected(doc.error());}
      auto live=out->lease->retain();if(!live){return std::unexpected(live.error());}
      if(!namedDirectory(out->lease->directoryHandle(),"dht",out->dht.get())||(network&&!namedDirectory(out->dht.get(),out->netName,out->net.get()))){return error(ErrorCode::Conflict,"Azureus version parent changed before recovery");}
      Descriptor pendingFd(::openat(out->dir(),"version.saving",O_RDONLY|O_NONBLOCK|O_CLOEXEC|O_NOFOLLOW));struct stat retained{};
      auto latest=capture(out->dir(),"version.saving");if(pendingFd.get()<0||::fstat(pendingFd.get(),&retained)!=0||!latest||!latest->stamp||!same(retained,*latest->stamp)||!equal(*latest,*pending)){return error(ErrorCode::Conflict,"Azureus pending version changed before recovery");}
      if(::renameat2(out->dir(),"version.saving",out->dir(),"version.dat",RENAME_NOREPLACE)!=0||::fsync(out->dir())!=0){return error(ErrorCode::Io,"Cannot durably recover Azureus pending version");}
      primary=capture(out->dir(),"version.dat");if(!primary){return std::unexpected(primary.error());}if(!primary->stamp||primary->stamp->st_dev!=retained.st_dev||primary->stamp->st_ino!=retained.st_ino||primary->bytes!=pending->bytes){return error(ErrorCode::Conflict,"Recovered Azureus version differs from held pending source");}
    }
    out->primary=std::move(*primary);auto valid=out->validate();if(!valid){return std::unexpected(valid.error());}return std::shared_ptr<AzureusValueVersions>(new AzureusValueVersions(std::move(out)));
  }catch(...){return error(ErrorCode::Io,"Cannot allocate Azureus version owner");}
}
std::expected<void,Error> AzureusValueVersions::validate()const {try{std::lock_guard guard(impl_->mutex);return impl_->validate();}catch(...){return error(ErrorCode::Io,"Cannot validate Azureus version owner");}}
std::expected<std::int32_t,Error> AzureusValueVersions::next(std::int64_t now){
  try{
    std::lock_guard guard(impl_->mutex);auto valid=impl_->validate();if(!valid){return std::unexpected(valid.error());}
    if(!impl_->remaining){
      auto doc=parse(impl_->primary.bytes.value_or("de"));if(!doc){return std::unexpected(doc.error());}const auto* saved=doc->find("next");const auto reserved=originalAzureusVersionReservation(saved?std::optional{saved->integer}:std::nullopt,now);
      if(!doc->replaceValue("next","i"+std::to_string(reserved.next)+'e')){return error(ErrorCode::InvalidFormat,"Cannot encode Azureus version reservation");}const auto bytes=doc->serialize();
      impl_->poisoned=true;auto created=detail::writeExclusiveOwned(impl_->dir(),"version.saving",bytes,0600);if(!created){return std::unexpected(created.error());}
      // All lease/parent/source/temporary preimages are rechecked before rename.
      auto held=impl_->lease->retain();if(!held){return std::unexpected(held.error());}
      auto primary=capture(impl_->dir(),"version.dat"),pending=capture(impl_->dir(),"version.saving");struct stat fd{};
      if(!primary||!pending||!equal(*primary,impl_->primary)||!pending->bytes||*pending->bytes!=bytes||!pending->stamp||::fstat(created->get(),&fd)!=0||!same(fd,*pending->stamp)||!namedDirectory(impl_->lease->directoryHandle(),"dht",impl_->dht.get())||(impl_->network&&!namedDirectory(impl_->dht.get(),impl_->netName,impl_->net.get()))){return error(ErrorCode::Conflict,"Azureus version publication preimage changed");}
      const auto flags=impl_->primary.bytes?0:RENAME_NOREPLACE;
      if(::renameat2(impl_->dir(),"version.saving",impl_->dir(),"version.dat",flags)!=0||::fsync(impl_->dir())!=0){return error(ErrorCode::Io,"Cannot durably publish Azureus version reservation");}
      auto published=capture(impl_->dir(),"version.dat");if(!published||published->bytes!=std::optional{bytes}||!published->stamp||published->stamp->st_dev!=fd.st_dev||published->stamp->st_ino!=fd.st_ino){return error(ErrorCode::Conflict,"Published Azureus version changed");}
      impl_->primary=std::move(*published);impl_->next=static_cast<std::uint32_t>(reserved.first);impl_->remaining=128;impl_->poisoned=false;
    }
    --impl_->remaining;const auto answer=std::bit_cast<std::int32_t>(impl_->next);++impl_->next;return answer;
  }catch(...){impl_->poisoned=true;return error(ErrorCode::Io,"Azureus version reservation failed");}
}
}
