// SPDX-License-Identifier: GPL-2.0-or-later
#include "saved_subscription_service.hpp"
#include "subscription_store_internal.hpp"
#include "file_io.hpp"
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <limits>
#include <QUrl>
#include <algorithm>
#include <chrono>
#include <map>
#include <thread>

namespace biglybtc::search {
namespace {
using S=SubscriptionServiceError;
std::int64_t wallNow(){return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();}
bool profileCurrent(const storage::ProfileLease& lease,const std::filesystem::path& path){std::error_code ec;if(!lease.valid()||!path.is_absolute()||std::filesystem::canonical(path,ec)!=path||ec){return false;}storage::detail::Descriptor named(::open(path.c_str(),O_RDONLY|O_CLOEXEC|O_DIRECTORY|O_NOFOLLOW));struct stat held{},label{};return named.get()>=0&&::fstat(lease.directoryHandle(),&held)==0&&::fstat(named.get(),&label)==0&&S_ISDIR(held.st_mode)&&held.st_uid==::geteuid()&&held.st_dev==label.st_dev&&held.st_ino==label.st_ino;}
S sourceError(SavedRssSourceError e){switch(e){case SavedRssSourceError::WrongWorker:return S::WrongWorker;case SavedRssSourceError::Unsupported:return S::Unsupported;case SavedRssSourceError::Limit:return S::Limit;case SavedRssSourceError::Memory:return S::Memory;default:return S::Source;}}
}
struct SavedSubscriptionService::Impl {
  std::shared_ptr<const storage::ProfileLease> lease;std::filesystem::path profile;std::uint64_t session=0;std::thread::id worker=std::this_thread::get_id();SubscriptionServiceMode mode=SubscriptionServiceMode::ReadOnlyPreview;SubscriptionFetchFactory factory;
  std::optional<SavedSubscriptionSourceSnapshot> source;std::unique_ptr<RssHttpFetch> fetch;std::shared_ptr<std::stop_source> stop;
  using Callback=std::stop_callback<std::function<void()>>;std::unique_ptr<Callback> receiver;
  struct Failure {std::int64_t time=0;unsigned count=0;};std::map<std::string,Failure> failures;
  std::map<QString,std::chrono::steady_clock::time_point> rateAttempts;std::size_t cursor=0;bool closed=false;
  void retire(){receiver.reset();if(fetch){fetch->close();fetch.reset();}source.reset();stop.reset();}
  bool valid(const storage::ProfileLease& caller,std::uint64_t current)const{return !closed&&lease&&current==session&&caller.sharesOwnership(*lease)&&profileCurrent(caller,profile);}
  std::expected<void,S> guard(const storage::ProfileLease& caller,std::uint64_t current)const{if(worker!=std::this_thread::get_id()){return std::unexpected(S::WrongWorker);}if(!valid(caller,current)){return std::unexpected(S::Source);}return {};}
  std::expected<bool,S> rateAllowed(const storage::ProfileLease& caller,const SavedSubscriptionSourceSnapshot& snapshot,bool manual){
    auto access=caller.sourceAccess();if(!access||!access->observationsAllowed()){return std::unexpected(S::Source);}auto config=storage::ConfigFile::load(caller,"biglybt.config",{1024*1024,1024*1024,65536,64});if(!config){return config.error().code==storage::ErrorCode::Missing?std::expected<bool,S>{true}:std::unexpected(S::Source);}
    const auto* policy=config->document().find("subscriptions.config.rate_limits");if(!policy){return true;}if(policy->type!=core::Node::Type::Bytes||policy->bytes.size()>4096){return std::unexpected(S::Unsupported);}const auto host=QUrl(QString::fromStdString(snapshot.description().literalGetUrl)).host();
    for(const auto& entry:QString::fromUtf8(policy->bytes.data(),static_cast<qsizetype>(policy->bytes.size())).split(',')){const auto pair=entry.trimmed().split('=');if(pair.size()!=2||pair[0].trimmed()!=host){continue;}bool ok=false;const auto minutes=pair[1].trimmed().toLongLong(&ok);if(!ok||minutes<=0){continue;}if(minutes>std::numeric_limits<std::int64_t>::max()/60000){return std::unexpected(S::Limit);}const auto now=std::chrono::steady_clock::now();const auto old=rateAttempts.find(host);if(old!=rateAttempts.end()&&std::chrono::duration_cast<std::chrono::milliseconds>(now-old->second).count()<minutes*60000){if(manual){return std::unexpected(S::Busy);}return false;}if(old==rateAttempts.end()&&rateAttempts.size()>=4096){return std::unexpected(S::Limit);}rateAttempts[host]=now;
    }return true;
  }
};
SavedSubscriptionService::SavedSubscriptionService(std::unique_ptr<Impl> value):impl_(std::move(value)){}
SavedSubscriptionService::~SavedSubscriptionService(){cancel();if(impl_){impl_->retire();impl_->lease.reset();}}
std::expected<std::unique_ptr<SavedSubscriptionService>,S> SavedSubscriptionService::open(const storage::ProfileLease& lease,std::filesystem::path profile,std::uint64_t session,SubscriptionServiceMode mode,SubscriptionFetchFactory factory){try{
  if(!session||!profileCurrent(lease,profile)){return std::unexpected(S::Source);}auto retained=lease.retain();if(!retained){return std::unexpected(S::Source);}auto value=std::make_unique<Impl>();value->lease=*retained;value->profile=std::move(profile);value->session=session;value->mode=mode;value->factory=std::move(factory);
  if(mode==SubscriptionServiceMode::Normal){auto recovered=detail::recoverSubscriptionWrite(lease,value->profile);if(!recovered){return std::unexpected(S::PendingWrite);}}
  return std::unique_ptr<SavedSubscriptionService>(new SavedSubscriptionService(std::move(value)));
}catch(const std::bad_alloc&){return std::unexpected(S::Memory);}}
std::expected<std::vector<SubscriptionSavedRow>,S> SavedSubscriptionService::catalog(const storage::ProfileLease& caller,std::uint64_t session){try{
  auto valid=impl_->guard(caller,session);if(!valid){return std::unexpected(valid.error());}auto access=caller.sourceAccess();if(!access||!access->observationsAllowed()){return std::unexpected(S::Source);}auto config=storage::ConfigFile::load(caller,"subscriptions.config",{4*1024*1024,4*1024*1024,100000,64});if(!config){return config.error().code==storage::ErrorCode::Missing?std::expected<std::vector<SubscriptionSavedRow>,S>{std::vector<SubscriptionSavedRow>{}}:std::unexpected(S::Source);}auto rows=parseSubscriptionIndex(config->document().originalBytes());if(!rows){return std::unexpected(S::Source);}if(config->recoverySource()!=storage::RecoverySource::Primary){for(auto& row:rows->rows){row.issue=SubscriptionValueError::Unsupported;}}return std::move(rows->rows);
}catch(const std::bad_alloc&){return std::unexpected(S::Memory);}}
std::expected<SubscriptionResults,S> SavedSubscriptionService::results(const storage::ProfileLease& caller,std::uint64_t session,SavedSubscriptionSelector selected){auto valid=impl_->guard(caller,session);if(!valid){return std::unexpected(valid.error());}auto values=detail::readSubscriptionHistory(caller,impl_->profile,selected);if(!values){return std::unexpected(sourceError(values.error()));}return std::move(*values);}
std::expected<void,S> SavedSubscriptionService::check(const storage::ProfileLease& caller,std::uint64_t session,SavedSubscriptionSelector selected,std::stop_token receiver){return beginCheck(caller,session,std::move(selected),receiver,true);}
std::expected<void,S> SavedSubscriptionService::beginCheck(const storage::ProfileLease& caller,std::uint64_t session,SavedSubscriptionSelector selected,std::stop_token receiver,bool manual){try{
  auto valid=impl_->guard(caller,session);if(!valid){return valid;}if(impl_->mode!=SubscriptionServiceMode::Normal){return std::unexpected(S::Unsupported);}if(receiver.stop_requested()){return std::unexpected(S::Cancelled);}if(!impl_->failures.contains(selected.shortId)&&impl_->failures.size()>=4096){return std::unexpected(S::Limit);}if(impl_->fetch){return std::unexpected(S::Busy);}
  auto source=SavedSubscriptionSourceSnapshot::read(caller,impl_->profile,session,std::move(selected));if(!source){return std::unexpected(sourceError(source.error()));}if(!manual){const auto& policy=source->metadata();const auto now=wallNow();if(!policy.subscribed||!policy.enabled||subscriptionNextScan(policy.lastScan,policy.intervalOverride,policy.intervalMinutes,policy.defaultCheckMinutes,now)>now){return std::unexpected(S::Busy);}}auto allowed=impl_->rateAllowed(caller,*source,manual);if(!allowed){return std::unexpected(allowed.error());}if(!*allowed){return std::unexpected(S::Busy);}
  auto stopped=std::make_shared<std::stop_source>();std::unique_ptr<RssHttpFetch> fetch;if(impl_->factory){fetch=impl_->factory(caller,impl_->profile,session,source->description().selected,stopped->get_token());}else{auto opened=RssHttpFetch::openSubscriptionOneShot(caller,impl_->profile,session,source->description().selected,stopped->get_token());if(opened){fetch=std::move(*opened);}else if(opened.error()==RssFetchError::Busy){return std::unexpected(S::Busy);}}
  if(!fetch){return std::unexpected(S::Source);}const auto state=fetch->observation();if(!fetch->matchesSubscriptionSource(caller,impl_->profile,session,source->description().selected,stopped->get_token())||state.phase!=RssFetchPhase::BeforeDNS||state.gets||state.resolverSubmissions||state.failure||receiver.stop_requested()||!source->current(caller,session)){fetch->requestCancel();fetch->close();return std::unexpected(receiver.stop_requested()?S::Cancelled:S::Source);}
  impl_->source.emplace(std::move(*source));impl_->stop=std::move(stopped);impl_->fetch=std::move(fetch);impl_->receiver=std::make_unique<Impl::Callback>(receiver,[weak=std::weak_ptr<std::stop_source>(impl_->stop)]{if(const auto stop=weak.lock()){stop->request_stop();}});return {};
}catch(const std::bad_alloc&){impl_->retire();return std::unexpected(S::Memory);}}
std::expected<void,S> SavedSubscriptionService::changeResultState(const storage::ProfileLease& caller,std::uint64_t session,SavedSubscriptionSelector selected,const std::vector<std::string>& keys,SubscriptionResultAction action){
  auto valid=impl_->guard(caller,session);if(!valid){return valid;}if(impl_->mode!=SubscriptionServiceMode::Normal){return std::unexpected(S::Unsupported);}if(impl_->fetch){return std::unexpected(S::Busy);}auto source=SavedSubscriptionSourceSnapshot::read(caller,impl_->profile,session,std::move(selected));if(!source){return std::unexpected(sourceError(source.error()));}auto value=source->prepareState(keys,action==SubscriptionResultAction::MarkRead,action==SubscriptionResultAction::Delete,wallNow());if(!value){return std::unexpected(sourceError(value.error()));}auto saved=source->publish(caller,session,*value);if(!saved){return std::unexpected(S::PendingWrite);}return {};
}
std::expected<std::optional<SubscriptionCheckOutcome>,S> SavedSubscriptionService::poll(const storage::ProfileLease& caller,std::uint64_t session){try{
  auto valid=impl_->guard(caller,session);if(!valid){return std::unexpected(valid.error());}
  if(impl_->fetch){
    const auto selected=impl_->source->description().selected;if(impl_->stop->stop_requested()){impl_->fetch->requestCancel();}auto result=impl_->fetch->poll(caller,session);if(result&&!*result){return std::optional<SubscriptionCheckOutcome>{};}SubscriptionCheckOutcome outcome;outcome.selected=selected;const bool cancelled=impl_->stop->stop_requested();
    if(cancelled||(!result&&result.error()==RssFetchError::Cancelled)){outcome.cancelled=true;}else if(!result){outcome.failure=S::Source;}else if(!impl_->source->current(caller,session)){outcome.failure=S::Source;}else{
      auto value=impl_->source->prepareCheck((**result).document,wallNow());if(!value){outcome.failure=sourceError(value.error());}else if(impl_->stop->stop_requested()){outcome.cancelled=true;}else{auto applied=impl_->source->publish(caller,session,*value);outcome.stored=bool(applied);if(!applied){outcome.failure=S::PendingWrite;}}
    }
    impl_->retire();if(outcome.stored){impl_->failures.erase(selected.shortId);}else if(!outcome.cancelled&&outcome.failure&&*outcome.failure!=S::PendingWrite){if(impl_->failures.contains(selected.shortId)||impl_->failures.size()<4096){auto& fail=impl_->failures[selected.shortId];fail.time=wallNow();if(fail.count<64){++fail.count;}}}return std::optional{std::move(outcome)};
  }
  if(impl_->mode!=SubscriptionServiceMode::Normal){return std::optional<SubscriptionCheckOutcome>{};}auto rows=catalog(caller,session);if(!rows){return std::unexpected(rows.error());}if(rows->empty()){return std::optional<SubscriptionCheckOutcome>{};}const auto& row=(*rows)[impl_->cursor++%rows->size()];if(!row.subscribed||!row.enabled||row.issue){return std::optional<SubscriptionCheckOutcome>{};}const auto now=wallNow();const auto prior=impl_->failures.find(row.selected.shortId);if(prior!=impl_->failures.end()&&subscriptionRetryTime(prior->second.time,prior->second.count)>now){return std::optional<SubscriptionCheckOutcome>{};}
  auto source=SavedSubscriptionSourceSnapshot::read(caller,impl_->profile,session,row.selected);if(!source){return std::optional<SubscriptionCheckOutcome>{};}const auto& metadata=source->metadata();if(subscriptionNextScan(metadata.lastScan,metadata.intervalOverride,metadata.intervalMinutes,metadata.defaultCheckMinutes,now)>now){return std::optional<SubscriptionCheckOutcome>{};}
  // check() makes its own real-source snapshot/factory. This local observation
  // never grants an HTTP phase, and no read gate spans that construction.
  auto started=beginCheck(caller,session,row.selected,{},false);if(!started&&started.error()!=S::Busy&&started.error()!=S::Limit&&(impl_->failures.contains(row.selected.shortId)||impl_->failures.size()<4096)){auto& fail=impl_->failures[row.selected.shortId];fail.time=now;if(fail.count<64){++fail.count;}}return std::optional<SubscriptionCheckOutcome>{};
}catch(const std::bad_alloc&){impl_->retire();return std::unexpected(S::Memory);}}
void SavedSubscriptionService::cancel()noexcept{if(impl_&&impl_->worker==std::this_thread::get_id()&&impl_->stop){impl_->stop->request_stop();}}
bool SavedSubscriptionService::active()const noexcept{return impl_&&bool(impl_->fetch);}
std::expected<void,S> SavedSubscriptionService::close(){if(!impl_||impl_->closed){return {};}if(impl_->worker!=std::this_thread::get_id()){return std::unexpected(S::WrongWorker);}cancel();impl_->retire();if(impl_->mode==SubscriptionServiceMode::Normal&&impl_->lease){auto recovered=detail::recoverSubscriptionWrite(*impl_->lease,impl_->profile);if(!recovered){return std::unexpected(S::PendingWrite);}}impl_->closed=true;impl_->lease.reset();return {};}
} // namespace biglybtc::search
