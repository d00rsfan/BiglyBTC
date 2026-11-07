// SPDX-License-Identifier: GPL-2.0-or-later
#include "saved_metasearch_service.hpp"
#include "metasearch_document.hpp"
#include <QRandomGenerator>
#include <fcntl.h>
#include <cerrno>
#include <sys/stat.h>
#include <unistd.h>
#include <map>
#include <set>
#include <thread>
#include <tuple>
namespace biglybtc::search {
namespace {
using E=MetasearchServiceError;
bool profileCurrent(const storage::ProfileLease& lease,const std::filesystem::path& profile){
  std::error_code error;if(!lease.valid()||!profile.is_absolute()||std::filesystem::canonical(profile,error)!=profile||error){return false;}
  const int fd=::open(profile.c_str(),O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW);if(fd<0){return false;}struct stat a{},b{};const bool ok=::fstat(fd,&a)==0&&::fstat(lease.directoryHandle(),&b)==0&&a.st_dev==b.st_dev&&a.st_ino==b.st_ino&&a.st_uid==::geteuid();::close(fd);return ok;
}
}
struct SavedMetasearchService::Impl {
  std::shared_ptr<const storage::ProfileLease> lease;
  std::filesystem::path profile;std::uint64_t session=0,nonce=0,nextRow=1;
  std::thread::id worker=std::this_thread::get_id();MetasearchServiceMode mode=MetasearchServiceMode::ReadOnlyPreview;
  MetasearchFetchFactory factory;bool closed=false,running=false,failed=false;
  std::vector<SavedMetasearchSelector> selected;std::string query;std::size_t cursor=0;
  std::shared_ptr<std::stop_source> lifetime;
  using Callback=std::stop_callback<std::function<void()>>;std::unique_ptr<Callback> receiver;
  std::unique_ptr<MetasearchHttpFetch> fetch;std::shared_ptr<const SavedMetasearchSourceSnapshot> source;
  struct Retained {std::shared_ptr<const SavedMetasearchSourceSnapshot> source;MetasearchRowValues row;};
  std::map<std::uint64_t,Retained> retained;
  std::expected<void,E> guard(const storage::ProfileLease& caller,std::uint64_t current)const{
    if(worker!=std::this_thread::get_id()){return std::unexpected(E::WrongWorker);}if(closed){return std::unexpected(E::Closed);}
    if(!lease||current!=session||!caller.sharesOwnership(*lease)||!profileCurrent(caller,profile)){return std::unexpected(E::Source);}return {};
  }
  void retireFetch(){if(fetch){fetch->requestCancel();fetch->close();fetch.reset();}source.reset();}
  void invalidate(){if(lifetime){lifetime->request_stop();}retireFetch();receiver.reset();retained.clear();running=false;}
  MetasearchSearchUpdate finish(MetasearchEngineOutcome value){
    if(value.failure||value.partial){failed=true;}retireFetch();++cursor;running=cursor<selected.size();
    return {nonce,cursor,selected.size(),!running,false,failed,std::move(value)};
  }
};
SavedMetasearchService::SavedMetasearchService(std::unique_ptr<Impl> impl):impl_(std::move(impl)){}
SavedMetasearchService::~SavedMetasearchService(){if(impl_){impl_->invalidate();}}
std::expected<std::unique_ptr<SavedMetasearchService>,E> SavedMetasearchService::open(const storage::ProfileLease& lease,std::filesystem::path profile,std::uint64_t session,MetasearchServiceMode mode,MetasearchFetchFactory factory){try{
  if(!session||!profileCurrent(lease,profile)||(mode!=MetasearchServiceMode::Normal&&mode!=MetasearchServiceMode::ReadOnlyPreview)){return std::unexpected(E::Source);}auto retained=lease.retain();if(!retained){return std::unexpected(E::Source);}
  auto value=std::make_unique<Impl>();value->lease=*retained;value->profile=std::move(profile);value->session=session;value->mode=mode;value->factory=std::move(factory);return std::unique_ptr<SavedMetasearchService>(new SavedMetasearchService(std::move(value)));
}catch(const std::bad_alloc&){return std::unexpected(E::Memory);}}
std::expected<std::vector<MetasearchCatalogRow>,E> SavedMetasearchService::catalog(const storage::ProfileLease& caller,std::uint64_t session){try{
  auto valid=impl_->guard(caller,session);if(!valid){return std::unexpected(valid.error());}auto gate=caller.sourceAccess();if(!gate||!gate->observationsAllowed()){return std::unexpected(E::Source);}
  auto saved=storage::ConfigFile::load(caller,"metasearch.config",{4*1024*1024,4*1024*1024,100000,64});if(!saved){if(saved.error().code==storage::ErrorCode::Missing){return std::vector<MetasearchCatalogRow>{};}return std::unexpected(E::Source);}
  auto value=readMetasearchCatalog(saved->document().originalBytes());if(!value){return std::unexpected(E::Source);}
  struct stat pending{};const bool staging=::fstatat(caller.directoryHandle(),"metasearch.config.saving",&pending,AT_SYMLINK_NOFOLLOW)==0;if(!staging&&errno!=ENOENT){return std::unexpected(E::Source);}if(staging){for(auto& row:value->rows){row.issue=MetasearchValueError::Unsupported;row.diagnostic="The saved engine file has a pending update";}}
  if(saved->recoverySource()!=storage::RecoverySource::Primary){for(auto& row:value->rows){row.issue=MetasearchValueError::Unsupported;row.diagnostic="The saved engine file needs recovery review";}}
  return std::move(value->rows);
}catch(const std::bad_alloc&){return std::unexpected(E::Memory);}}
std::expected<std::uint64_t,E> SavedMetasearchService::beginSearch(const storage::ProfileLease& caller,std::uint64_t session,std::vector<SavedMetasearchSelector> selected,std::string query,std::stop_token receiver){try{
  auto valid=impl_->guard(caller,session);if(!valid){return std::unexpected(valid.error());}if(impl_->mode!=MetasearchServiceMode::Normal){return std::unexpected(E::Unsupported);}if(receiver.stop_requested()){return std::unexpected(E::Cancelled);}if(selected.empty()||selected.size()>8){return std::unexpected(E::Limit);}
  std::set<std::tuple<std::size_t,std::int64_t,std::string>> unique;for(const auto& row:selected){if(row.uid.empty()||row.uid.size()>256||!unique.emplace(row.sourceIndex,row.id,row.uid).second){return std::unexpected(E::Source);}}
  if(!expandMetasearchQuery("https://feeds.example.org/search?q=%s",query)){return std::unexpected(E::Unsupported);}
  impl_->invalidate();impl_->selected=std::move(selected);impl_->query=std::move(query);impl_->cursor=0;impl_->failed=false;impl_->nextRow=1;
  do{impl_->nonce=QRandomGenerator::system()->generate64();}while(!impl_->nonce);impl_->lifetime=std::make_shared<std::stop_source>();std::weak_ptr<std::stop_source> weak=impl_->lifetime;
  impl_->receiver=std::make_unique<Impl::Callback>(receiver,[weak]{if(const auto live=weak.lock()){live->request_stop();}});impl_->running=true;return impl_->nonce;
}catch(const std::bad_alloc&){impl_->invalidate();return std::unexpected(E::Memory);}}
std::expected<std::optional<MetasearchSearchUpdate>,E> SavedMetasearchService::poll(const storage::ProfileLease& caller,std::uint64_t session){try{
  auto valid=impl_->guard(caller,session);if(!valid){return std::unexpected(valid.error());}if(!impl_->running){return std::optional<MetasearchSearchUpdate>{};}
  if(impl_->lifetime->stop_requested()){const auto nonce=impl_->nonce,total=impl_->selected.size(),completed=impl_->cursor;impl_->invalidate();return std::optional{MetasearchSearchUpdate{nonce,completed,total,true,true,impl_->failed,{}}};}
  MetasearchEngineOutcome outcome;outcome.engine=impl_->selected[impl_->cursor];
  if(!impl_->fetch){
    auto actual=SavedMetasearchSourceSnapshot::read(caller,impl_->profile,session,outcome.engine,impl_->query);
    if(!actual){outcome.failure=E::Source;outcome.diagnostic="The selected engine or its source policy is unavailable";return std::optional{impl_->finish(std::move(outcome))};}
    impl_->source=std::make_shared<const SavedMetasearchSourceSnapshot>(std::move(*actual));const auto token=impl_->lifetime->get_token();
    if(impl_->factory){impl_->fetch=impl_->factory(caller,impl_->profile,session,outcome.engine,impl_->query,token);}else{auto opened=MetasearchHttpFetch::openOneShot(caller,impl_->profile,session,outcome.engine,impl_->query,token);if(opened){impl_->fetch=std::move(*opened);}}
    const auto observed=impl_->fetch?impl_->fetch->observation():RssFetchObservation{};
    if(!impl_->fetch||!impl_->fetch->matchesQuery(caller,impl_->profile,session,outcome.engine,impl_->query,token)||observed.phase!=RssFetchPhase::BeforeDNS||observed.gets||observed.resolverSubmissions||observed.failure||impl_->lifetime->stop_requested()||!impl_->source->current(caller,session)){
      outcome.failure=E::Source;outcome.diagnostic="The search owner does not match the selected engine, query and receiver";return std::optional{impl_->finish(std::move(outcome))};
    }
  }
  auto fetched=impl_->fetch->poll(caller,session);if(fetched&&!*fetched){return std::optional<MetasearchSearchUpdate>{};}
  if(!fetched){outcome.failure=fetched.error()==RssFetchError::Cancelled?E::Cancelled:E::Fetch;outcome.diagnostic="This search engine did not return a document";return std::optional{impl_->finish(std::move(outcome))};}
  const auto observed=impl_->fetch->observation();if(!observed.joined||(**fetched).session!=session||(**fetched).nonce!=observed.nonce||impl_->lifetime->stop_requested()||!impl_->source->current(caller,session)){outcome.failure=E::Source;outcome.diagnostic="Search results became stale before delivery";return std::optional{impl_->finish(std::move(outcome))};}
  auto document=parseMetasearchDocument(*impl_->source->description().recipe,(**fetched).bytes,(**fetched).context);
  if(!document){outcome.failure=E::Parse;outcome.diagnostic="This engine returned an unsupported or malformed result document";return std::optional{impl_->finish(std::move(outcome))};}
  if(impl_->lifetime->stop_requested()||!impl_->source->current(caller,session)){outcome.failure=E::Source;outcome.diagnostic="Search results became stale while being decoded";return std::optional{impl_->finish(std::move(outcome))};}
  outcome.partial=document->partial;for(auto& row:document->rows){const MetasearchResultKey key{impl_->nonce,impl_->nextRow++};impl_->retained.emplace(key.rowId,Impl::Retained{impl_->source,row});outcome.rows.push_back({key,outcome.engine,std::move(row)});}
  return std::optional{impl_->finish(std::move(outcome))};
}catch(const std::bad_alloc&){impl_->invalidate();return std::unexpected(E::Memory);}}
std::expected<SavedMetasearchResultHandle,E> SavedMetasearchService::retainResult(const storage::ProfileLease& caller,std::uint64_t session,MetasearchResultKey key){
  auto valid=impl_->guard(caller,session);if(!valid){return std::unexpected(valid.error());}if(impl_->mode!=MetasearchServiceMode::Normal||!impl_->lifetime||impl_->lifetime->stop_requested()||key.queryNonce!=impl_->nonce){return std::unexpected(E::Source);}const auto found=impl_->retained.find(key.rowId);if(found==impl_->retained.end()){return std::unexpected(E::Source);}
  auto handle=SavedMetasearchResultHandle::mint(caller,session,found->second.source,found->second.row,key,impl_->lifetime);if(!handle){return std::unexpected(E::Unsupported);}return std::move(*handle);
}
bool SavedMetasearchService::active()const noexcept{return impl_&&impl_->running;}
void SavedMetasearchService::cancel()noexcept{if(impl_&&impl_->worker==std::this_thread::get_id()&&impl_->lifetime){impl_->lifetime->request_stop();}}
std::expected<void,E> SavedMetasearchService::close(){if(!impl_||impl_->closed){return {};}if(impl_->worker!=std::this_thread::get_id()){return std::unexpected(E::WrongWorker);}impl_->invalidate();impl_->closed=true;impl_->lease.reset();return {};}
} // namespace biglybtc::search
