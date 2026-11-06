// SPDX-License-Identifier: GPL-2.0-or-later
#include "saved_rss_source.hpp"
#include "file_io.hpp"
#include "library_http_trust.hpp"
#include <QHostAddress>
#include <QRegularExpression>
#include <QUrl>
#include <algorithm>
#include <array>
#include <cerrno>
#include <fcntl.h>
#include <limits>
#include <new>
#include <set>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>

namespace biglybtc::search {
namespace {
#include "rss_source_internal.inc"
} // namespace

struct SavedRssSourceSnapshot::Impl {
  std::shared_ptr<const storage::ProfileLease> lease;
  std::filesystem::path profile;
  std::uint64_t session=0;
  std::thread::id worker;
  struct stat directoryIdentity{};
  std::vector<File> files;
  SavedRssDescription description;
};
SavedRssSourceSnapshot::SavedRssSourceSnapshot(std::unique_ptr<Impl> value)noexcept:impl_(std::move(value)){}
SavedRssSourceSnapshot::SavedRssSourceSnapshot(SavedRssSourceSnapshot&&)noexcept=default;
SavedRssSourceSnapshot& SavedRssSourceSnapshot::operator=(SavedRssSourceSnapshot&&)noexcept=default;
SavedRssSourceSnapshot::~SavedRssSourceSnapshot()=default;
const SavedRssDescription& SavedRssSourceSnapshot::description()const noexcept{return impl_->description;}
std::expected<SavedRssSourceSnapshot,E> SavedRssSourceSnapshot::read(const storage::ProfileLease& lease,
    const std::filesystem::path& profile,std::uint64_t session,SavedRssSelector selector,SavedRssReadReason reason){
  try{
    if(!session||reason!=SavedRssReadReason::ExplicitOneShotDescription){return std::unexpected(E::Unsupported);}
    auto access=lease.sourceAccess();if(!access||!access->observationsAllowed()){return std::unexpected(E::Lease);}
    auto root=directory(lease,profile);if(!root){return std::unexpected(root.error());}
    auto alias=lease.retain();if(!alias){return std::unexpected(E::Lease);}
    auto data=std::make_unique<Impl>();data->lease=std::move(*alias);data->profile=profile;data->session=session;
    data->worker=std::this_thread::get_id();data->directoryIdentity=*root;
    for(const auto stem:{"metasearch.config","biglybt.config",".certs"}){
      for(const auto suffix:{"",".bak",".saving"}){
        const auto name=std::string(stem)+suffix;auto file=named(lease.directoryHandle(),name,cap(name));
        if(!file){return std::unexpected(file.error());}if(*suffix&&std::string_view(suffix)==".saving"&&file->present){return std::unexpected(E::SourceChanged);}
        data->files.push_back(std::move(*file));
      }
    }
    auto lock=named(lease.directoryHandle(),".azlock",cap(".azlock"));if(!lock||!lock->present){return std::unexpected(E::Lease);}data->files.push_back(std::move(*lock));
    if(!data->files[0].present){return std::unexpected(E::Malformed);}
    if(!data->files[3].present&&data->files[4].present){return std::unexpected(E::SourceChanged);}
    if(!data->files[6].present&&data->files[7].present){return std::unexpected(E::SourceChanged);}
    auto catalog=core::Document::parse(data->files[0].bytes,ConfigLimits);if(!catalog){return std::unexpected(E::Malformed);}
    auto value=describe(*catalog,selector);if(!value){return std::unexpected(value.error());}
    auto config=core::Document::parse(data->files[3].present?data->files[3].bytes:"de",{SettingsBytes,SettingsBytes,65536,64});
    if(!config){return std::unexpected(E::Malformed);}auto supported=settings(config->root(),selector.id);if(!supported){return std::unexpected(supported.error());}
    if(value->literalGetUrl.starts_with("https://")){
      auto trust=engine::detail::libraryHttpTrust(lease);if(!trust){return std::unexpected(E::Unsupported);}value->trustAnchors=std::move(trust->anchors);
    }
    data->description=std::move(*value);
    auto stable=unchanged(lease.directoryHandle(),data->files);if(!stable){return std::unexpected(stable.error());}
    return SavedRssSourceSnapshot(std::move(data));
  }catch(const std::bad_alloc&){return std::unexpected(E::Memory);}
}
std::expected<std::uint64_t,E> SavedRssSourceSnapshot::current(const storage::ProfileLease& caller,std::uint64_t session)const{
  try{
    if(!impl_||impl_->worker!=std::this_thread::get_id()){return std::unexpected(E::WrongWorker);}
    if(session!=impl_->session||!caller.sharesOwnership(*impl_->lease)){return std::unexpected(E::Lease);}
    auto access=caller.sourceAccess();if(!access||!access->observationsAllowed()){return std::unexpected(E::SourceChanged);}
    auto root=directory(caller,impl_->profile);
    if(!root||root->st_dev!=impl_->directoryIdentity.st_dev||root->st_ino!=impl_->directoryIdentity.st_ino){return std::unexpected(E::SourceChanged);}
    auto stable=unchanged(caller.directoryHandle(),impl_->files);if(!stable){return std::unexpected(stable.error());}
    return access->revision();
  }catch(const std::bad_alloc&){return std::unexpected(E::Memory);}
}
} // namespace biglybtc::search
