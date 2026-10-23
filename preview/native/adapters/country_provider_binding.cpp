#include "country_provider_binding.hpp"
#include "file_io.hpp"
#include "biglybtc/plugin_host.hpp"
#include <openssl/evp.h>
#include <array>
#include <cerrno>
#include <cstring>
#include <limits>
#include <memory>
#include <new>
#include <optional>
#include <string_view>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace biglybtc::storage {
namespace {
using detail::Descriptor;
constexpr std::uint64_t MaximumModuleBytes = 64U * 1024U * 1024U;
struct Failure { Error error; };
[[noreturn]] void fail(ErrorCode code, const char* message) { throw Failure{{code,message}}; }
[[noreturn]] void io(const char* message) { if(errno==ENOENT){fail(ErrorCode::Missing,message);}fail(ErrorCode::Io,message); }
bool same(const struct stat& a,const struct stat& b) noexcept {
  return a.st_dev==b.st_dev&&a.st_ino==b.st_ino&&a.st_mode==b.st_mode&&a.st_uid==b.st_uid&&a.st_nlink==b.st_nlink
      &&a.st_size==b.st_size&&a.st_mtim.tv_sec==b.st_mtim.tv_sec&&a.st_mtim.tv_nsec==b.st_mtim.tv_nsec
      &&a.st_ctim.tv_sec==b.st_ctim.tv_sec&&a.st_ctim.tv_nsec==b.st_ctim.tv_nsec;
}
bool lowerHex(std::string_view text) noexcept {
  for(char c:text){if(!((c>='0'&&c<='9')||(c>='a'&&c<='f'))){return false;}}return true;
}
bool validVersion(std::string_view text) noexcept {
  if(text.empty()||text.size()>64){return false;}unsigned dots=0,digits=0;
  for(char c:text){if(c=='.'){if(!digits||++dots>3){return false;}digits=0;}else if(c>='0'&&c<='9'){++digits;}else{return false;}}
  return dots>=1&&digits!=0;
}
struct DirectoryLeaf { Descriptor parent; std::string name; };
DirectoryLeaf parentOf(const std::filesystem::path& path) {
  const auto raw=path.native();
  if(!path.is_absolute()||path.lexically_normal()!=path||raw.find('\0')!=std::string::npos
      ||raw.size()>4096||path.filename().empty()||path.filename()=="."||path.filename()=="..") {
    fail(ErrorCode::UnsafePath,"Country asset path must be absolute, normalized and explicit");
  }
  std::optional<Descriptor> directory(std::in_place,::open("/",O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW));if(directory->get()<0){io("Cannot open country asset path root");}
  for(const auto& component:path.parent_path().relative_path()){
    if(component.empty()||component=="."||component==".."){fail(ErrorCode::UnsafePath,"Invalid country asset parent");}
    Descriptor next(::openat(directory->get(),component.c_str(),O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK));
    if(next.get()<0){io("Cannot open country asset parent without following links");}
    directory.emplace(next.release());
  }
  return {std::move(*directory),path.filename().string()};
}
Descriptor openAssetDirectory(const std::filesystem::path& path) {
  auto parent=parentOf(path);Descriptor fd(::openat(parent.parent.get(),parent.name.c_str(),O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK));
  if(fd.get()<0){io("Cannot open explicit country database directory");}struct stat info{};
  if(::fstat(fd.get(),&info)!=0){io("Cannot inspect country database directory");}
  if(!S_ISDIR(info.st_mode)||info.st_uid!=::geteuid()||(info.st_mode&0077)!=0||!info.st_nlink){
    fail(ErrorCode::UnsafePath,"Country database directory must be owned and private");}
  return fd;
}
std::string digest(int fd,std::uint64_t expectedBytes) {
  std::unique_ptr<EVP_MD_CTX,decltype(&EVP_MD_CTX_free)> context(EVP_MD_CTX_new(),EVP_MD_CTX_free);
  if(!context||EVP_DigestInit_ex(context.get(),EVP_sha256(),nullptr)!=1){fail(ErrorCode::Io,"Cannot initialize country module authentication");}
  std::array<unsigned char,64*1024> buffer{};std::uint64_t count=0;
  for(;;){const auto n=::read(fd,buffer.data(),buffer.size());if(n<0&&errno==EINTR){continue;}if(n<0){io("Cannot read approved country module");}if(!n){break;}
    const auto amount=static_cast<std::uint64_t>(n);if(amount>expectedBytes-count){fail(ErrorCode::Conflict,"Country module length changed during authentication");}
    count+=amount;if(EVP_DigestUpdate(context.get(),buffer.data(),static_cast<std::size_t>(n))!=1){fail(ErrorCode::Io,"Cannot authenticate country module bytes");}}
  if(count!=expectedBytes){fail(ErrorCode::Conflict,"Country module length differs from approved release");}
  std::array<unsigned char,EVP_MAX_MD_SIZE> result{};unsigned length=0;
  if(EVP_DigestFinal_ex(context.get(),result.data(),&length)!=1||length!=32){fail(ErrorCode::Io,"Cannot finish country module authentication");}
  constexpr char hex[]="0123456789abcdef";std::string out(64,'0');
  for(std::size_t i=0;i<32;++i){out[i*2]=hex[result[i]>>4];out[i*2+1]=hex[result[i]&15];}return out;
}
}
std::expected<std::unique_ptr<CountryProvider>,Error> loadApprovedCountryProvider(
    const ProfileLease& lease,const std::filesystem::path& artifact,const CountryModuleApproval& approval,
    const std::filesystem::path& databaseDirectory) {
  try {
    if(!lease.valid()){fail(ErrorCode::Busy,"Country provider requires a held profile lease");}
    struct stat held{};if(::fstat(lease.directoryHandle(),&held)!=0){io("Cannot inspect country profile lease");}
    if(!S_ISDIR(held.st_mode)||held.st_uid!=::geteuid()||!held.st_nlink){fail(ErrorCode::UnsafePath,"Country profile lease directory is unavailable");}
    if(approval.sha256.size()!=64||!lowerHex(approval.sha256)||!approval.bytes||approval.bytes>MaximumModuleBytes||!validVersion(approval.version)){
      fail(ErrorCode::InvalidFormat,"Invalid app-owned country module approval");}
    auto db=openAssetDirectory(databaseDirectory);auto parent=parentOf(artifact);
    Descriptor module(::openat(parent.parent.get(),parent.name.c_str(),O_RDONLY|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK));
    if(module.get()<0){io("Cannot open approved country module");}struct stat before{},after{};
    if(::fstat(module.get(),&before)!=0){io("Cannot inspect approved country module");}
    if(!S_ISREG(before.st_mode)||before.st_uid!=::geteuid()||before.st_nlink!=1||(before.st_mode&0022)!=0
        ||before.st_size<=0||static_cast<std::uint64_t>(before.st_size)!=approval.bytes){fail(ErrorCode::UnsafePath,"Country module differs from safe approved artifact shape");}
    if(digest(module.get(),approval.bytes)!=approval.sha256){fail(ErrorCode::Conflict,"Country module SHA256 differs from app-owned approval");}
    if(::fstat(module.get(),&after)!=0){io("Cannot recheck approved country module");}
    if(!same(before,after)){fail(ErrorCode::Conflict,"Country module changed during authentication");}
    // The descriptor route loads this exact authenticated inode. Trusted bundle
    // files remain immutable while loaded; this is not a native-code sandbox.
    const bt_plugin_options_v1 options{sizeof(bt_plugin_options_v1),BT_PLUGIN_ABI_V1,db.get(),0,"0.0",nullptr,nullptr};
    auto loaded=plugins::Module::loadDescriptor(module.get(),"azlocprov",BT_CAP_LOCATION,options,approval.version);
    if(!loaded){return std::unexpected(Error{ErrorCode::Unsupported,loaded.error().message});}
    auto provider=CountryProvider::bind(std::move(*loaded));
    if(!provider){fail(ErrorCode::Unsupported,"Cannot bind approved country location provider");}
    return std::move(*provider);
  }catch(const Failure& failure){return std::unexpected(failure.error);}
  catch(const std::bad_alloc&){return std::unexpected(Error{ErrorCode::Io,"Not enough memory to bind country provider"});}
  catch(const std::filesystem::filesystem_error&){return std::unexpected(Error{ErrorCode::UnsafePath,"Invalid country asset path"});}
}
} // namespace biglybtc::storage
