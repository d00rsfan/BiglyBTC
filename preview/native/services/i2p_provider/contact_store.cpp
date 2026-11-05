// SPDX-License-Identifier: GPL-2.0-or-later
#include "contact_store.hpp"
#include "file_io.hpp"
#include <array>
#include <cerrno>
#include <cstdio>
#include <fcntl.h>
#include <mutex>
#include <sys/stat.h>
#include <unistd.h>

namespace biglybtc::storage {
namespace {
namespace dht=services::i2p::dht;
using detail::Descriptor;
std::unexpected<Error> error(ErrorCode code,const char* message){return std::unexpected(Error{code,message});}
bool sameFile(const struct stat& a,const struct stat& b){
  return a.st_dev==b.st_dev&&a.st_ino==b.st_ino&&a.st_mode==b.st_mode&&a.st_uid==b.st_uid&&a.st_gid==b.st_gid&&a.st_nlink==b.st_nlink&&a.st_size==b.st_size&&a.st_mtim.tv_sec==b.st_mtim.tv_sec&&a.st_mtim.tv_nsec==b.st_mtim.tv_nsec&&a.st_ctim.tv_sec==b.st_ctim.tv_sec&&a.st_ctim.tv_nsec==b.st_ctim.tv_nsec;
}
bool sameDirectory(const struct stat& a,const struct stat& b){
  return S_ISDIR(a.st_mode)&&S_ISDIR(b.st_mode)&&a.st_dev==b.st_dev&&a.st_ino==b.st_ino&&a.st_mode==b.st_mode&&a.st_uid==b.st_uid&&a.st_gid==b.st_gid;
}
struct Input {
  Descriptor fd{-1};
  std::optional<struct stat> stamp;
  std::optional<std::string> bytes;
};
// Key capture deliberately never reads, parses, hashes or exports private bytes.
// Both presence and the held inode/stat are bound; dht*.config is independent.
std::expected<Input,Error> capture(int dir,const std::string& name,bool contents){
  Input out;out.fd=Descriptor(::openat(dir,name.c_str(),O_RDONLY|O_NOFOLLOW|O_NONBLOCK|O_CLOEXEC));
  if(out.fd.get()<0){
    if(errno!=ENOENT){return error(ErrorCode::UnsafePath,"Cannot safely open I2P contact input");}
    struct stat named{};if(::fstatat(dir,name.c_str(),&named,AT_SYMLINK_NOFOLLOW)==0||errno!=ENOENT){return error(ErrorCode::Conflict,"I2P contact input presence changed");}return out;
  }
  struct stat before{},after{},named{};
  if(::fstat(out.fd.get(),&before)!=0||!S_ISREG(before.st_mode)||before.st_uid!=::geteuid()||before.st_nlink!=1||before.st_size<0||(contents&&static_cast<std::uintmax_t>(before.st_size)>dht::MaximumSavedContactsBytes)){
    return error(ErrorCode::UnsafePath,"Unsafe I2P contact input inode");
  }
  if(contents){
    out.bytes.emplace(static_cast<std::size_t>(before.st_size),'\0');std::size_t at=0;
    while(at<out.bytes->size()){
      const auto n=::pread(out.fd.get(),out.bytes->data()+at,out.bytes->size()-at,static_cast<off_t>(at));
      if(n<0&&errno==EINTR){continue;}if(n<=0){return error(ErrorCode::Conflict,"I2P contact input changed while reading");}at+=static_cast<std::size_t>(n);
    }
    char extra{};ssize_t n;do{n=::pread(out.fd.get(),&extra,1,before.st_size);}while(n<0&&errno==EINTR);
    if(n!=0){return error(ErrorCode::Conflict,"I2P contact input grew while reading");}
  }
  if(::fstat(out.fd.get(),&after)!=0||::fstatat(dir,name.c_str(),&named,AT_SYMLINK_NOFOLLOW)!=0||!sameFile(before,after)||!sameFile(after,named)){
    return error(ErrorCode::Conflict,"I2P contact input name changed while reading");
  }
  out.stamp=after;return out;
}
bool equal(const Input& a,const Input& b){return a.bytes==b.bytes&&a.stamp.has_value()==b.stamp.has_value()&&(!a.stamp||sameFile(*a.stamp,*b.stamp));}
bool heldUnchanged(const Input& input){struct stat actual{};return !input.stamp||(input.fd.get()>=0&&::fstat(input.fd.get(),&actual)==0&&sameFile(actual,*input.stamp));}
struct Directory {Descriptor fd{-1};struct stat stamp{};};
std::expected<Directory,Error> directory(int parent,const std::string& name,bool create=false){
  Directory out;out.fd=Descriptor(::openat(parent,name.c_str(),O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC));
  if(out.fd.get()<0&&errno==ENOENT&&create){
    if(::mkdirat(parent,name.c_str(),0700)!=0&&errno!=EEXIST){return error(ErrorCode::Io,"Cannot create original I2P contact directory");}
    if(::fsync(parent)!=0){return error(ErrorCode::Io,"Cannot sync original I2P contact parent");}
    out.fd=Descriptor(::openat(parent,name.c_str(),O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC));
  }
  struct stat named{};
  if(out.fd.get()<0||::fstat(out.fd.get(),&out.stamp)!=0||out.stamp.st_uid!=::geteuid()||::fstatat(parent,name.c_str(),&named,AT_SYMLINK_NOFOLLOW)!=0||!sameDirectory(out.stamp,named)){
    return error(ErrorCode::UnsafePath,"Owned original I2P contact directory required");
  }return out;
}
bool namedDirectory(int parent,const std::string& name,const Directory& directory){
  struct stat held{},named{};return ::fstat(directory.fd.get(),&held)==0&&::fstatat(parent,name.c_str(),&named,AT_SYMLINK_NOFOLLOW)==0&&sameDirectory(held,directory.stamp)&&sameDirectory(named,directory.stamp);
}
std::expected<std::string,Error> profilePath(int fd){
  std::array<char,4097> value{};const auto n=::readlink(("/proc/self/fd/"+std::to_string(fd)).c_str(),value.data(),value.size());
  if(n<=0||static_cast<std::size_t>(n)>=value.size()||value[0]!='/'){return error(ErrorCode::UnsafePath,"Named original profile directory required");}return std::string(value.data(),static_cast<std::size_t>(n));
}
bool profileNamed(const std::string& path,int held){
  Descriptor current(::open("/",O_RDONLY|O_DIRECTORY|O_CLOEXEC));if(current.get()<0||path.empty()||path[0]!='/'){return false;}
  std::size_t at=1;while(at<path.size()){
    const auto end=path.find('/',at);const auto part=path.substr(at,end==std::string::npos?path.size()-at:end-at);
    if(part.empty()||part=="."||part==".."){return false;}
    Descriptor next(::openat(current.get(),part.c_str(),O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC));if(next.get()<0){return false;}current=std::move(next);
    if(end==std::string::npos){break;}at=end+1;
  }
  struct stat actual{},named{};return ::fstat(held,&actual)==0&&::fstat(current.get(),&named)==0&&sameDirectory(actual,named)&&actual.st_uid==::geteuid();
}
std::expected<std::vector<dht::SavedContact>,Error> decode(const std::string& bytes){
  auto value=dht::decodeSavedContacts(services::i2p::View(reinterpret_cast<const services::i2p::Byte*>(bytes.data()),bytes.size()));
  if(!value){return error(ErrorCode::InvalidFormat,"Invalid or unsupported original I2P contact cache");}return std::move(*value);
}
bool cancelled(std::stop_token stop,std::chrono::steady_clock::time_point end){return stop.stop_requested()||std::chrono::steady_clock::now()>=end;}
}

struct I2pContacts::Impl {
  std::shared_ptr<const ProfileLease> lease;
  std::string profile,cacheName,keyName;
  struct stat profileStamp{};
  Directory plugins,helper,cache;
  Input key,primary,pending;
  unsigned index=0;bool secondary=false,canWrite=false,poisoned=false;
  std::vector<dht::SavedContact> contacts;
  mutable std::mutex mutex;
  std::expected<void,Error> roots()const{
    auto retained=lease->retain();if(!retained){return std::unexpected(retained.error());}
    struct stat actual{};
    if(::fstat(lease->directoryHandle(),&actual)!=0||!sameDirectory(actual,profileStamp)||!profileNamed(profile,lease->directoryHandle())||!namedDirectory(lease->directoryHandle(),"plugins",plugins)||!namedDirectory(plugins.fd.get(),"azneti2phelper",helper)||!namedDirectory(helper.fd.get(),cacheName,cache)){
      return error(ErrorCode::Conflict,"Original I2P contact directory identity changed");
    }
    auto current=capture(helper.fd.get(),keyName,false);if(!current){return std::unexpected(current.error());}
    if(!heldUnchanged(key)||!equal(*current,key)){return error(ErrorCode::Conflict,"Original I2P key input changed");}return {};
  }
  std::expected<void,Error> validate()const{
    if(poisoned){return error(ErrorCode::Conflict,"I2P contact cache requires explicit recovery");}
    auto valid=roots();if(!valid){return valid;}
    auto p=capture(cache.fd.get(),"contacts.dat",true),s=capture(cache.fd.get(),"contacts.saving",true);
    if(!p){return std::unexpected(p.error());}if(!s){return std::unexpected(s.error());}
    if(!heldUnchanged(primary)||!heldUnchanged(pending)||!equal(*p,primary)||!equal(*s,pending)){
      return error(ErrorCode::Conflict,"Original I2P contact file changed");
    }return {};
  }
};
I2pContacts::I2pContacts(std::unique_ptr<Impl> value):impl_(std::move(value)){}
I2pContacts::~I2pContacts()=default;
unsigned I2pContacts::index()const noexcept{return impl_->index;}
bool I2pContacts::secondary()const noexcept{return impl_->secondary;}
std::shared_ptr<const ProfileLease> I2pContacts::profileOwnership()const noexcept{return impl_->lease;}
std::expected<std::shared_ptr<I2pContacts>,Error> I2pContacts::open(const ProfileLease& lease,unsigned index,bool secondary){
  try{
    if(index>1){return error(ErrorCode::InvalidFormat,"Original I2P contact index must be zero or one");}
    auto access=lease.sourceAccess(true);if(!access){return std::unexpected(access.error());}
    auto retained=lease.retain();if(!retained){return std::unexpected(retained.error());}
    auto out=std::make_unique<Impl>();out->lease=std::move(*retained);out->index=index;out->secondary=secondary;
    auto path=profilePath(out->lease->directoryHandle());if(!path){return std::unexpected(path.error());}out->profile=std::move(*path);
    if(::fstat(out->lease->directoryHandle(),&out->profileStamp)!=0||!profileNamed(out->profile,out->lease->directoryHandle())){return error(ErrorCode::UnsafePath,"Original I2P profile pathname changed");}
    auto plugins=directory(out->lease->directoryHandle(),"plugins");if(!plugins){return std::unexpected(plugins.error());}out->plugins=std::move(*plugins);
    auto helper=directory(out->plugins.fd.get(),"azneti2phelper");if(!helper){return std::unexpected(helper.error());}out->helper=std::move(*helper);
    const std::string suffix=(index?"1":"")+std::string(secondary?"_s":"");out->cacheName="dhtdata"+suffix;out->keyName="dest_key"+suffix+".dat";
    auto key=capture(out->helper.fd.get(),out->keyName,false);if(!key){return std::unexpected(key.error());}out->key=std::move(*key);
    auto cache=directory(out->helper.fd.get(),out->cacheName,true);if(!cache){return std::unexpected(cache.error());}out->cache=std::move(*cache);
    auto primary=capture(out->cache.fd.get(),"contacts.dat",true),pending=capture(out->cache.fd.get(),"contacts.saving",true);
    if(!primary){return std::unexpected(primary.error());}if(!pending){return std::unexpected(pending.error());}
    const auto& selected=primary->bytes?primary->bytes:pending->bytes;
    if(selected){auto contacts=decode(*selected);if(!contacts){return std::unexpected(contacts.error());}out->contacts=std::move(*contacts);}
    out->canWrite=!(primary->stamp&&pending->stamp);out->primary=std::move(*primary);out->pending=std::move(*pending);
    auto valid=out->validate();if(!valid){return std::unexpected(valid.error());}
    if(!out->primary.stamp&&out->pending.stamp){
      const auto original=*out->pending.stamp;
      if(::renameat2(out->cache.fd.get(),"contacts.saving",out->cache.fd.get(),"contacts.dat",RENAME_NOREPLACE)!=0||::fsync(out->cache.fd.get())!=0){return error(ErrorCode::Io,"Cannot durably recover original I2P contact cache");}
      auto p=capture(out->cache.fd.get(),"contacts.dat",true),s=capture(out->cache.fd.get(),"contacts.saving",true);
      if(!p||!s||!p->stamp||s->stamp||p->stamp->st_dev!=original.st_dev||p->stamp->st_ino!=original.st_ino||p->bytes!=out->pending.bytes){return error(ErrorCode::Conflict,"Recovered I2P contact inode changed");}
      out->primary=std::move(*p);out->pending=std::move(*s);valid=out->validate();if(!valid){return std::unexpected(valid.error());}
    }
    return std::shared_ptr<I2pContacts>(new I2pContacts(std::move(out)));
  }catch(...){return error(ErrorCode::Io,"Cannot open original I2P contact cache");}
}
std::expected<void,Error> I2pContacts::validate()const{
  try{auto access=impl_->lease->sourceAccess();if(!access){return std::unexpected(access.error());}std::lock_guard lock(impl_->mutex);return impl_->validate();}
  catch(...){return error(ErrorCode::Io,"Cannot validate original I2P contact cache");}
}
std::expected<std::shared_ptr<const I2pContactsSnapshot>,Error> I2pContacts::snapshot()const{
  try{auto access=impl_->lease->sourceAccess();if(!access){return std::unexpected(access.error());}std::lock_guard lock(impl_->mutex);auto valid=impl_->validate();if(!valid){return std::unexpected(valid.error());}return std::make_shared<const I2pContactsSnapshot>(I2pContactsSnapshot{impl_->contacts,impl_->canWrite});}
  catch(...){return error(ErrorCode::Io,"Cannot copy original I2P contact cache");}
}
std::expected<void,Error> I2pContacts::save(std::span<const dht::SavedContact> values,std::stop_token stop,std::chrono::steady_clock::time_point end){
  try{
    auto access=impl_->lease->sourceAccess(true);if(!access){return std::unexpected(access.error());}std::lock_guard lock(impl_->mutex);
    auto valid=impl_->validate();if(!valid){return valid;}if(!impl_->canWrite){return error(ErrorCode::Unsupported,"Ambiguous original I2P contact files remain unchanged");}
    if(values.size()>dht::MaximumSavedContacts){return error(ErrorCode::Unsupported,"Original I2P contact export maximum32");}
    // Copy and validate the owning snapshot before any filesystem effect.
    std::vector<dht::SavedContact> accepted(values.begin(),values.end());
    auto encoded=dht::encodeSavedContacts(accepted);if(!encoded){return error(ErrorCode::InvalidFormat,"Invalid original I2P contact export");}
    const std::string bytes(reinterpret_cast<const char*>(encoded->data()),encoded->size());
    if(cancelled(stop,end)){return error(ErrorCode::Cancelled,"I2P contact export cancelled or expired");}
    impl_->poisoned=true;auto created=detail::writeExclusiveOwned(impl_->cache.fd.get(),"contacts.saving",bytes,0600);if(!created){return std::unexpected(created.error());}
    valid=impl_->roots();if(!valid){return valid;}
    auto primary=capture(impl_->cache.fd.get(),"contacts.dat",true),pending=capture(impl_->cache.fd.get(),"contacts.saving",true);struct stat actual{};
    if(!primary||!pending||!heldUnchanged(impl_->primary)||!equal(*primary,impl_->primary)||!pending->stamp||pending->bytes!=std::optional{bytes}||::fstat(created->get(),&actual)!=0||!sameFile(actual,*pending->stamp)){
      return error(ErrorCode::Conflict,"I2P contact publication preimage changed");
    }
    if(cancelled(stop,end)){return error(ErrorCode::Cancelled,"I2P contact export cancelled or expired before publication");}
    // Cooperative writers share the OFD gate. These checks do not claim atomic
    // exclusion against an uncooperative pathname swap between check/rename.
    if(::renameat2(impl_->cache.fd.get(),"contacts.saving",impl_->cache.fd.get(),"contacts.dat",impl_->primary.stamp?0:RENAME_NOREPLACE)!=0||::fsync(impl_->cache.fd.get())!=0){return error(ErrorCode::Io,"Cannot durably publish original I2P contacts");}
    auto published=capture(impl_->cache.fd.get(),"contacts.dat",true),absent=capture(impl_->cache.fd.get(),"contacts.saving",true);
    if(!published||!absent||!published->stamp||absent->stamp||published->bytes!=std::optional{bytes}||published->stamp->st_dev!=actual.st_dev||published->stamp->st_ino!=actual.st_ino){return error(ErrorCode::Conflict,"Published I2P contact inode changed");}
    valid=impl_->roots();if(!valid){return valid;}
    impl_->primary=std::move(*published);impl_->pending=std::move(*absent);impl_->contacts=std::move(accepted);impl_->poisoned=false;return {};
  }catch(...){std::lock_guard lock(impl_->mutex);impl_->poisoned=true;return error(ErrorCode::Io,"Original I2P contact publication failed");}
}
} // namespace biglybtc::storage
