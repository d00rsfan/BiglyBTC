#include "profile_preflight.hpp"
#include "biglybtc/library.hpp"
#include "biglybtc/overall_document.hpp"
#include "encoded_document.hpp"
#include "file_io.hpp"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cstdint>
#include <cerrno>
#include <optional>
#include <ostream>
#include <stdexcept>
#include <set>

namespace biglybtc::app {
namespace {
constexpr std::size_t MaximumFile=64*1024*1024;
constexpr std::uint64_t MaximumAggregate=512*1024*1024;
using storage::detail::Descriptor;
bool same(const struct stat& a,const struct stat& b) {
  return a.st_dev==b.st_dev&&a.st_ino==b.st_ino&&a.st_size==b.st_size
      &&a.st_mode==b.st_mode&&a.st_uid==b.st_uid&&a.st_nlink==b.st_nlink
      &&a.st_mtim.tv_sec==b.st_mtim.tv_sec&&a.st_mtim.tv_nsec==b.st_mtim.tv_nsec
      &&a.st_ctim.tv_sec==b.st_ctim.tv_sec&&a.st_ctim.tv_nsec==b.st_ctim.tv_nsec;
}
storage::EncodedDocument read(int directory,const std::string& leaf,std::uint64_t& total) {
  Descriptor fd(::openat(directory,leaf.c_str(),O_RDONLY|O_NOFOLLOW|O_NONBLOCK|O_CLOEXEC|O_NOATIME));
  if(fd.get()<0){throw std::runtime_error("metadata unavailable");}
  struct stat before{},after{},named{};
  if(::fstat(fd.get(),&before)||!S_ISREG(before.st_mode)||before.st_uid!=::geteuid()
      ||before.st_nlink!=1||before.st_size<0||std::uint64_t(before.st_size)>MaximumFile
      ||std::uint64_t(before.st_size)>MaximumAggregate-total){throw std::runtime_error("unsafe metadata or limit");}
  std::string bytes(static_cast<std::size_t>(before.st_size),'\0');std::size_t offset=0;
  while(offset<bytes.size()){
    const auto count=::pread(fd.get(),bytes.data()+offset,bytes.size()-offset,static_cast<off_t>(offset));
    if(count<0&&errno==EINTR){continue;}
    if(count<=0){throw std::runtime_error("incomplete metadata read");}
    offset+=static_cast<std::size_t>(count);
  }
  if(::fstat(fd.get(),&after)||::fstatat(directory,leaf.c_str(),&named,AT_SYMLINK_NOFOLLOW)
      ||!same(before,after)||!same(after,named)){throw std::runtime_error("metadata changed during read");}
  total+=bytes.size();auto limits=storage::compatibleConfigLimits();limits.inputBytes=MaximumFile;
  auto decoded=storage::EncodedDocument::parse(std::move(bytes),limits);
  if(!decoded){throw std::runtime_error("unsupported metadata encoding");}
  return std::move(*decoded);
}
std::optional<std::int64_t> integer(const core::Document& d,std::string_view key) {
  const auto* v=d.find(key);if(!v||v->type!=core::Node::Type::Integer){return {};}
  return v->integer;
}
}
int checkExistingProfile(const std::filesystem::path& profile,std::ostream& out) {
  // This entry intentionally has no ProfileLease and cannot run recovery or
  // obtain writer authority while the Java client owns its normal lock.
  try {
    std::error_code ec;
    if(!profile.is_absolute()||profile.lexically_normal()!=profile
        ||std::filesystem::canonical(profile,ec)!=profile||ec){throw std::runtime_error("profile path");}
    Descriptor directory(::open(profile.c_str(),O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC));
    struct stat ds{};
    if(directory.get()<0||::fstat(directory.get(),&ds)||ds.st_uid!=::geteuid()){throw std::runtime_error("profile directory");}
    std::uint64_t bytes=0;
    const auto config=read(directory.get(),"biglybt.config",bytes);
    if(config.document().root().type!=core::Node::Type::Dictionary){throw std::runtime_error("configuration root");}
    const auto inventory=read(directory.get(),"downloads.config",bytes);
    auto library=core::readLibrary(inventory.document());
    if(!library){throw std::runtime_error("saved inventory");}
    Descriptor active(::openat(directory.get(),"active",O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC));
    struct stat as{};
    if(active.get()<0||::fstat(active.get(),&as)||as.st_uid!=::geteuid()){throw std::runtime_error("state directory");}
    std::size_t readable=0,failed=library->issues.size(),publicOnly=0,publicOverlay=0,overlayOnly=0,v1=0,hybrid=0,v2=0,linked=0;
    const char* statisticsState="MISSING";struct stat statisticsName{};
    if(::fstatat(directory.get(),"BiglyBT.statistics",&statisticsName,AT_SYMLINK_NOFOLLOW)==0){
      try{const auto stats=read(directory.get(),"BiglyBT.statistics",bytes);
        const auto values=core::readOverallDocument(stats.document().originalBytes());
        if(values){statisticsState="READABLE";}else{statisticsState="REVIEW_REQUIRED";++failed;}
      }catch(const std::exception&){statisticsState="REVIEW_REQUIRED";++failed;}
    }else if(errno!=ENOENT){statisticsState="REVIEW_REQUIRED";++failed;}
    std::set<std::string> identities;
    for(const auto& row:library->downloads){
      if(!row.hash||!identities.insert(row.hash->hex()).second){++failed;continue;}
      try {
        const auto state=read(active.get(),row.hash->hex()+".dat",bytes);
        const auto* info=state.document().find("info");const auto* attrs=state.document().find("attributes");
        if(!info||info->type!=core::Node::Type::Dictionary||(attrs&&attrs->type!=core::Node::Type::Dictionary)){++failed;continue;}
        const auto* version=info->find("meta version");
        if(version){if(version->type!=core::Node::Type::Integer||version->integer!=2){++failed;continue;}
          if(info->find("pieces")){++hybrid;}else{++v2;}}
        else{++v1;}
        const auto* networks=attrs?attrs->find("networks"):nullptr;
        bool pub=!networks,overlay=false,valid=true;
        if(networks){
          if(networks->type!=core::Node::Type::List||networks->children.size()>16){valid=false;}
          else{for(const auto& n:networks->children){if(n.type!=core::Node::Type::Bytes){valid=false;break;}
            if(n.bytes=="Public"){pub=true;}else if(n.bytes=="I2P"||n.bytes=="Tor"){overlay=true;}else{valid=false;}}}
        }
        if(!valid){++failed;continue;}
        if(pub){if(overlay){++publicOverlay;}else{++publicOnly;}}else{++overlayOnly;}
        const auto* links=attrs?attrs->find("filelinks2"):nullptr;
        if(links&&links->type==core::Node::Type::List&&!links->children.empty()){++linked;}
        ++readable;
      }catch(const std::exception&){++failed;}
    }
    const auto port=integer(config.document(),"TCP.Listen.Port").value_or(6881);
    const auto udp=integer(config.document(),"UDP.Listen.Port").value_or(6881);
    const auto ipv6=integer(config.document(),"IPV6 Enable Support").value_or(0)!=0;
    out<<"{\"status\":\""<<(failed?"REVIEW_REQUIRED":"METADATA_READABLE")
       <<"\",\"rows\":"<<library->downloads.size()<<",\"readable\":"<<readable<<",\"issues\":"<<failed
       <<",\"public\":"<<publicOnly<<",\"public_with_inactive_overlays\":"<<publicOverlay<<",\"overlay_only\":"<<overlayOnly
       <<",\"v1\":"<<v1<<",\"hybrid\":"<<hybrid<<",\"v2\":"<<v2<<",\"linked\":"<<linked
       <<",\"tcp_port\":"<<port<<",\"udp_port\":"<<udp<<",\"ipv6\":"<<(ipv6?"true":"false")
       <<",\"statistics\":\""<<statisticsState<<"\""
       <<",\"metadata_bytes\":"<<bytes
       <<",\"payload_reads\":0,\"writes\":0,\"profile_lock_acquired\":false,\"cross_file_snapshot\":false"
         ",\"payload_and_runtime_compatibility\":\"NOT_CHECKED\"}\n";
    return failed?1:0;
  }catch(const std::exception&){
    out<<"{\"status\":\"PREFLIGHT_UNAVAILABLE\",\"payload_reads\":0,\"writes\":0,\"profile_lock_acquired\":false}\n";
    return 2;
  }
}
}
