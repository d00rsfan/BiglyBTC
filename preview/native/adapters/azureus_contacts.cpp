// SPDX-License-Identifier: GPL-2.0-or-later
#include "azureus_contacts.hpp"
#include "file_io.hpp"
#include <bit>
#include <cerrno>
#include <fcntl.h>
#include <mutex>
#include <sys/stat.h>
#include <unistd.h>
namespace biglybtc::storage {
namespace {
using detail::Descriptor;
constexpr std::size_t MaximumInput=MaximumAzureusContactFile;
std::unexpected<Error> error(ErrorCode c,const char* text){return std::unexpected(Error{c,text});}
bool same(const struct stat& a,const struct stat& b){return a.st_dev==b.st_dev&&a.st_ino==b.st_ino&&a.st_mode==b.st_mode&&a.st_uid==b.st_uid&&a.st_nlink==b.st_nlink&&a.st_size==b.st_size&&a.st_mtim.tv_sec==b.st_mtim.tv_sec&&a.st_mtim.tv_nsec==b.st_mtim.tv_nsec&&a.st_ctim.tv_sec==b.st_ctim.tv_sec&&a.st_ctim.tv_nsec==b.st_ctim.tv_nsec;}
struct Capture {std::optional<std::string> bytes;std::optional<struct stat> stamp;};
std::expected<Capture,Error> capture(int dir,const char* name){
  Capture out;struct stat value{};
  if(::fstatat(dir,name,&value,AT_SYMLINK_NOFOLLOW)==0){
    if(!S_ISREG(value.st_mode)||value.st_uid!=::geteuid()||value.st_nlink!=1||value.st_size<0||value.st_size>static_cast<off_t>(MaximumInput)){return error(ErrorCode::UnsafePath,"Unsafe Azureus contacts input");}out.stamp=value;
  }else if(errno!=ENOENT){return error(ErrorCode::Io,"Cannot inspect Azureus contacts input");}
  auto read=detail::readFile(dir,name,MaximumInput);if(!read){return std::unexpected(read.error());}out.bytes=std::move(*read);
  struct stat after{};const bool present=::fstatat(dir,name,&after,AT_SYMLINK_NOFOLLOW)==0;
  if(!present&&errno!=ENOENT){return error(ErrorCode::Io,"Cannot recheck Azureus contacts input");}
  if(present!=out.stamp.has_value()||present!=out.bytes.has_value()||(present&&!same(*out.stamp,after))){return error(ErrorCode::Conflict,"Azureus contacts input changed while reading");}return out;
}
bool equal(const Capture& a,const Capture& b){return a.bytes==b.bytes&&a.stamp.has_value()==b.stamp.has_value()&&(!a.stamp||same(*a.stamp,*b.stamp));}
std::expected<Descriptor,Error> directory(int parent,const std::string& name){
  Descriptor fd(::openat(parent,name.c_str(),O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW));
  if(fd.get()<0&&errno==ENOENT){if(::mkdirat(parent,name.c_str(),0700)!=0&&errno!=EEXIST){return error(ErrorCode::Io,"Cannot create Azureus contacts directory");}if(::fsync(parent)!=0){return error(ErrorCode::Io,"Cannot sync Azureus contacts parent");}fd=Descriptor(::openat(parent,name.c_str(),O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW));}
  struct stat held{},named{};if(fd.get()<0||::fstat(fd.get(),&held)!=0||::fstatat(parent,name.c_str(),&named,AT_SYMLINK_NOFOLLOW)!=0||!S_ISDIR(held.st_mode)||!S_ISDIR(named.st_mode)||held.st_uid!=::geteuid()||held.st_dev!=named.st_dev||held.st_ino!=named.st_ino){return error(ErrorCode::UnsafePath,"Unsafe Azureus contacts directory");}return fd;
}
bool namedDirectory(int parent,const std::string& name,int held){struct stat a{},b{};return ::fstat(held,&a)==0&&::fstatat(parent,name.c_str(),&b,AT_SYMLINK_NOFOLLOW)==0&&S_ISDIR(a.st_mode)&&S_ISDIR(b.st_mode)&&a.st_uid==::geteuid()&&a.st_dev==b.st_dev&&a.st_ino==b.st_ino;}
}

std::expected<std::vector<AzureusSavedContact>,Error> decodeAzureusContactState(std::string_view wire){
  if(wire.size()<4||wire.size()>MaximumAzureusContactFile){return error(ErrorCode::InvalidFormat,"Bounded original contact state required");}
  std::size_t at=0;const auto get=[&](unsigned count){std::uint64_t value=0;while(count--){value=(value<<8)|static_cast<unsigned char>(wire[at++]);}return value;};
  const auto count=get(4);if(count>MaximumAzureusSavedContacts){return error(ErrorCode::Unsupported,"Original contact state exceeds native32 envelope");}
  std::vector<AzureusSavedContact> out;out.reserve(count);
  for(std::uint64_t i=0;i<count;++i){
    if(wire.size()-at<11){return error(ErrorCode::InvalidFormat,"Truncated original contact record");}
    AzureusSavedContact value;value.timeAlive=std::bit_cast<std::int64_t>(get(8));const auto type=get(1);value.protocol=std::bit_cast<std::int8_t>(static_cast<std::uint8_t>(get(1)));value.length=static_cast<std::uint8_t>(get(1));
    if(type!=1||(value.length!=4&&value.length!=16)||wire.size()-at<value.length+std::size_t{2}){return error(ErrorCode::InvalidFormat,"Invalid original UDP contact record");}
    for(std::size_t j=0;j<value.length;++j){value.address[j]=static_cast<std::uint8_t>(get(1));}value.port=static_cast<std::uint16_t>(get(2));out.push_back(value);
  }
  if(at!=wire.size()){return error(ErrorCode::InvalidFormat,"Trailing original contact state bytes");}return out;
}
std::expected<std::string,Error> encodeAzureusContactState(std::span<const AzureusSavedContact> values){
  if(values.size()>MaximumAzureusSavedContacts){return error(ErrorCode::Unsupported,"Original contact export maximum32");}
  std::string out;out.reserve(MaximumAzureusContactFile);const auto put=[&](std::uint64_t value,unsigned count){for(unsigned i=count;i>0;--i){out.push_back(static_cast<char>(value>>((i-1)*8)));}};put(values.size(),4);
  for(const auto& value:values){if(value.length!=4&&value.length!=16){return error(ErrorCode::InvalidFormat,"Invalid original contact family");}put(static_cast<std::uint64_t>(value.timeAlive),8);put(1,1);put(static_cast<std::uint8_t>(value.protocol),1);put(value.length,1);for(std::size_t j=0;j<value.length;++j){put(value.address[j],1);}put(value.port,2);}return out;
}
namespace {
bool writable(std::span<const AzureusSavedContact> contacts,std::uint8_t family){
  for(const auto& value:contacts){
    if(value.length!=family||!value.port||value.protocol<51||value.protocol>55){return false;}
    if(family==4){if(value.address[0]!=127){return false;}}
    else{for(std::size_t i=0;i<15;++i){if(value.address[i]){return false;}}if(value.address[15]!=1){return false;}}
  }return true;
}
}
struct AzureusContacts::Impl {
  std::shared_ptr<const ProfileLease> lease;Descriptor dht{-1},net{-1};std::int32_t network=0;std::uint8_t family=4;std::string netName;
  Capture primary,pending;std::vector<AzureusSavedContact> contacts;bool canWrite=false,poisoned=false;mutable std::mutex mutex;
  int dir()const{return network?net.get():dht.get();}
  std::expected<void,Error> validate()const{
    if(poisoned){return error(ErrorCode::Conflict,"Azureus contacts require explicit recovery");}auto current=lease->retain();if(!current){return std::unexpected(current.error());}
    if(!namedDirectory(lease->directoryHandle(),"dht",dht.get())||(network&&!namedDirectory(dht.get(),netName,net.get()))){return error(ErrorCode::Conflict,"Azureus contact directory changed");}
    auto p=capture(dir(),"contacts.dat"),s=capture(dir(),"contacts.saving");if(!p){return std::unexpected(p.error());}if(!s){return std::unexpected(s.error());}
    if(!equal(*p,primary)||!equal(*s,pending)){return error(ErrorCode::Conflict,"Azureus contact source changed");}return {};
  }
};
AzureusContacts::AzureusContacts(std::unique_ptr<Impl> p):impl_(std::move(p)){}
AzureusContacts::~AzureusContacts()=default;
std::int32_t AzureusContacts::network()const noexcept{return impl_->network;}
std::uint8_t AzureusContacts::family()const noexcept{return impl_->family;}
std::shared_ptr<const ProfileLease> AzureusContacts::profileOwnership()const noexcept{return impl_->lease;}
std::expected<std::shared_ptr<AzureusContacts>,Error> AzureusContacts::open(const ProfileLease& source,std::int32_t network,std::uint8_t family){
  try{
    if(network<0||(family!=4&&family!=16)){return error(ErrorCode::InvalidFormat,"Invalid contact profile network/family");}auto access=source.sourceAccess(true);if(!access){return std::unexpected(access.error());}auto held=source.retain();if(!held){return std::unexpected(held.error());}
    auto out=std::make_unique<Impl>();out->lease=std::move(*held);out->network=network;out->family=family;auto dht=directory(out->lease->directoryHandle(),"dht");if(!dht){return std::unexpected(dht.error());}out->dht=std::move(*dht);
    if(network){out->netName="net"+std::to_string(network);auto net=directory(out->dht.get(),out->netName);if(!net){return std::unexpected(net.error());}out->net=std::move(*net);}
    auto primary=capture(out->dir(),"contacts.dat"),pending=capture(out->dir(),"contacts.saving");if(!primary){return std::unexpected(primary.error());}if(!pending){return std::unexpected(pending.error());}
    const auto selected=primary->bytes?primary->bytes:pending->bytes;
    if(selected){auto decoded=decodeAzureusContactState(*selected);if(!decoded){return std::unexpected(decoded.error());}out->contacts=std::move(*decoded);}
    out->canWrite=writable(out->contacts,family)&&!(primary->bytes&&pending->bytes);
    if(!primary->bytes&&pending->bytes&&out->canWrite){
      auto fresh=out->lease->retain();if(!fresh){return std::unexpected(fresh.error());}Descriptor fd(::openat(out->dir(),"contacts.saving",O_RDONLY|O_NONBLOCK|O_CLOEXEC|O_NOFOLLOW));struct stat actual{};auto latest=capture(out->dir(),"contacts.saving");
      if(fd.get()<0||::fstat(fd.get(),&actual)!=0||!latest||!latest->stamp||!same(actual,*latest->stamp)||!equal(*latest,*pending)||!namedDirectory(out->lease->directoryHandle(),"dht",out->dht.get())||(network&&!namedDirectory(out->dht.get(),out->netName,out->net.get()))){return error(ErrorCode::Conflict,"Contact pending recovery changed");}
      if(::renameat2(out->dir(),"contacts.saving",out->dir(),"contacts.dat",RENAME_NOREPLACE)!=0||::fsync(out->dir())!=0){return error(ErrorCode::Io,"Cannot durably recover original contact cache");}
      primary=capture(out->dir(),"contacts.dat");pending=capture(out->dir(),"contacts.saving");if(!primary||!pending||primary->bytes!=selected||!primary->stamp||primary->stamp->st_dev!=actual.st_dev||primary->stamp->st_ino!=actual.st_ino){return error(ErrorCode::Conflict,"Recovered contact bytes changed");}
    }
    out->primary=std::move(*primary);out->pending=std::move(*pending);auto valid=out->validate();if(!valid){return std::unexpected(valid.error());}return std::shared_ptr<AzureusContacts>(new AzureusContacts(std::move(out)));
  }catch(...){return error(ErrorCode::Io,"Cannot initialize contact cache owner");}
}
std::expected<void,Error> AzureusContacts::validate()const{try{auto access=impl_->lease->sourceAccess();if(!access){return std::unexpected(access.error());}std::lock_guard lock(impl_->mutex);return impl_->validate();}catch(...){return error(ErrorCode::Io,"Cannot validate contact cache");}}
std::expected<std::shared_ptr<const AzureusContactsSnapshot>,Error> AzureusContacts::snapshot()const{try{auto access=impl_->lease->sourceAccess();if(!access){return std::unexpected(access.error());}std::lock_guard lock(impl_->mutex);auto valid=impl_->validate();if(!valid){return std::unexpected(valid.error());}return std::make_shared<const AzureusContactsSnapshot>(AzureusContactsSnapshot{impl_->contacts,impl_->canWrite});}catch(...){return error(ErrorCode::Io,"Cannot copy contact cache");}}
std::expected<void,Error> AzureusContacts::save(std::span<const AzureusSavedContact> values,std::stop_token stop,std::chrono::steady_clock::time_point deadline){
  try{
    auto access=impl_->lease->sourceAccess(true);if(!access){return std::unexpected(access.error());}std::lock_guard lock(impl_->mutex);auto valid=impl_->validate();if(!valid){return std::unexpected(valid.error());}
    if(!impl_->canWrite||!writable(values,impl_->family)){return error(ErrorCode::Unsupported,"Foreign/ambiguous original cache remains unchanged");}auto bytes=encodeAzureusContactState(values);if(!bytes){return std::unexpected(bytes.error());}if(stop.stop_requested()||std::chrono::steady_clock::now()>=deadline){return error(ErrorCode::Cancelled,"Contact export cancelled/expired");}
    impl_->poisoned=true;auto created=detail::writeExclusiveOwned(impl_->dir(),"contacts.saving",*bytes,0600);if(!created){return std::unexpected(created.error());}
    auto held=impl_->lease->retain();if(!held){return std::unexpected(held.error());}auto primary=capture(impl_->dir(),"contacts.dat"),pending=capture(impl_->dir(),"contacts.saving");struct stat fd{};
    if(stop.stop_requested()||std::chrono::steady_clock::now()>=deadline){return error(ErrorCode::Cancelled,"Contact export cancelled/expired before publication");}
    if(!primary||!pending||!equal(*primary,impl_->primary)||!pending->bytes||*pending->bytes!=*bytes||!pending->stamp||::fstat(created->get(),&fd)!=0||!same(fd,*pending->stamp)||!namedDirectory(impl_->lease->directoryHandle(),"dht",impl_->dht.get())||(impl_->network&&!namedDirectory(impl_->dht.get(),impl_->netName,impl_->net.get()))){return error(ErrorCode::Conflict,"Contact publication preimage changed");}
    if(::renameat2(impl_->dir(),"contacts.saving",impl_->dir(),"contacts.dat",impl_->primary.bytes?0:RENAME_NOREPLACE)!=0||::fsync(impl_->dir())!=0){return error(ErrorCode::Io,"Cannot durably publish contacts");}
    auto published=capture(impl_->dir(),"contacts.dat");if(!published||published->bytes!=std::optional{*bytes}||!published->stamp||published->stamp->st_dev!=fd.st_dev||published->stamp->st_ino!=fd.st_ino){return error(ErrorCode::Conflict,"Published contacts changed");}
    impl_->primary=std::move(*published);impl_->pending={};impl_->contacts.assign(values.begin(),values.end());impl_->poisoned=false;return {};
  }catch(...){std::lock_guard lock(impl_->mutex);impl_->poisoned=true;return error(ErrorCode::Io,"Contact publication failed");}
}
}
