// SPDX-License-Identifier: GPL-2.0-or-later
#include "subscription_acquisition.hpp"
#include "subscription_association_values.hpp"
#include "subscription_values.hpp"
#include "file_io.hpp"
#include "original_filename_policy.hpp"
#include <QCryptographicHash>
#include <QRandomGenerator>
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <fcntl.h>
#include <limits>
#include <map>
#include <ranges>
#include <set>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <thread>
#include <unistd.h>
#include <linux/fs.h>

namespace biglybtc::app {
namespace {
#include "subscription_acquisition_values.inc"
#include "subscription_acquisition_io.inc"
// Closed crash-only seam in the separately compiled fixture Host. It cannot
// construct a source/plan or alter production admission. Production has no env
// lookup and always passes a null storage observer.
void crashCheckpoint(std::string_view stage){
#if defined(BIGLYBTC_RSS_FETCH_TEST_FIXTURE)
  const char* wanted=std::getenv("BIGLYBTC_SUBSCRIPTION_ACQUISITION_CRASH_AT");if(wanted&&stage==wanted){::_exit(86);}
#else
  (void)stage;
#endif
}
#if defined(BIGLYBTC_RSS_FETCH_TEST_FIXTURE)
void crashIntakeStage(storage::TorrentIntakeStage stage){switch(stage){case storage::TorrentIntakeStage::JournalSynced:crashCheckpoint("JournalSynced");break;case storage::TorrentIntakeStage::StatePublished:crashCheckpoint("StatePublished");break;case storage::TorrentIntakeStage::ArchivePublished:crashCheckpoint("ArchivePublished");break;case storage::TorrentIntakeStage::InventoryPublished:crashCheckpoint("InventoryPublished");break;}}
#endif
std::int64_t nowMillis(){return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();}
std::string resultsName(const search::SavedSubscriptionResultSelector& selected){return hex(selected.subscription.shortId)+".results";}
void sameValues(const AssociationReadValues& a,const AssociationReadValues& b){require(a.association==b.association&&a.results==b.results&&a.finalConfiguration==b.finalConfiguration,storage::ErrorCode::Conflict,"Acquisition subscription postimages do not match the owned attempt");}
}
struct SavedSubscriptionAcquisition::Impl {
  std::shared_ptr<const storage::ProfileLease> lease;
  std::filesystem::path profile,destination;
  std::uint64_t session=0;
  std::thread::id worker=std::this_thread::get_id();
  search::SavedSubscriptionResultSelector selected;
  std::optional<search::SavedSubscriptionResultSnapshot> source;
  std::unique_ptr<search::SubscriptionMetainfoFetch> fetch;
  std::shared_ptr<std::stop_source> stopped=std::make_shared<std::stop_source>();
  std::optional<std::stop_callback<std::function<void()>>> receiver;
  std::optional<storage::PreparedStoppedPublicIntake> plan;
  std::optional<Intent> intent;
  std::string ownerBytes,tempDirectory,tempScope,tempHash;
  std::optional<core::TorrentHash> hash;
  Outcome result;
  bool ready=false,terminal=false,closed=false;

  void guard(const storage::ProfileLease& caller,std::uint64_t current)const {
    require(!closed&&lease&&worker==std::this_thread::get_id()&&session==current&&current&&lease->sharesOwnership(caller),storage::ErrorCode::Conflict,"Acquisition worker, session or held profile changed");
    profileCurrent(caller,profile);
  }
  void closeFetch(){if(fetch){fetch->requestCancel();fetch->close();fetch.reset();}}
  void cleanupTemp(){if(!tempDirectory.empty()){Intent temporary;temporary.tempDirectory=tempDirectory;temporary.tempScope=tempScope;temporary.tempHash=tempHash;cleanupTemporary(temporary);tempDirectory.clear();}}
  void persisted(storage::ProfileLease& caller){const auto next=encodeIntent(*intent);{auto access=take(caller.sourceAccess(true));require(access.observationsAllowed(),storage::ErrorCode::Conflict,"Nested acquisition source write");publishIntent(caller.directoryHandle(),ownerBytes,*intent);}ownerBytes=next;}
  void validateValues(const core::Node& frozen)const {
    require(plan&&intent,storage::ErrorCode::Conflict,"Acquisition plan is absent");
    require(field(frozen,"schema")=="BiglyBTC.Subscription.Acquisition.Source.V1"&&field(frozen,"profile")==profile.string(),storage::ErrorCode::Conflict,"Acquisition profile binding changed");
    require(selectedBytes(selected)==intent->selection,storage::ErrorCode::Conflict,"Acquisition result identity changed");
    if(intent->values){require(intent->when.has_value(),storage::ErrorCode::InvalidFormat,"Acquisition time missing");sameValues(*intent->values,associationRead(field(frozen,"configuration"),field(frozen,"results"),selected,plan->images().hash,*intent->when));}
  }
  // Called only after storage independently proves all three exact postimages.
  Outcome settle(storage::ProfileLease& caller,bool allowNewValues) {
    require(plan&&intent&&take(plan->inspect(caller,ownerBytes))==storage::PreparedStoppedPublicIntake::State::Committed,storage::ErrorCode::Conflict,"Stopped intake completion is not proven");
    result.imported=true;result.hash=plan->images().hash;hash=result.hash;
    auto frozen=document(intent->source);validateValues(frozen.root());
    auto child=sourceDirectory(caller,frozen.root());
    const auto name=resultsName(selected);
    const auto beforeConfig=field(frozen.root(),"configuration"),beforeResults=field(frozen.root(),"results");
    const auto configBackup=optionalField(frozen.root(),"configuration_backup"),resultsBackup=optionalField(frozen.root(),"results_backup");
    bool current=false;
    {auto access=take(caller.sourceAccess());require(access.observationsAllowed(),storage::ErrorCode::Conflict,"Nested acquisition source observation");current=staticBindings(caller.directoryHandle(),child.get(),frozen.root());}
    const bool stopBeforeValues=!intent->done&&(stopped->stop_requested()||!current||!allowNewValues);
    if(!intent->values&&!intent->stopped&&!stopBeforeValues){
      // Initial A/R/F construction requires the unchanged actual before pair.
      {auto access=take(caller.sourceAccess());require(access.observationsAllowed(),storage::ErrorCode::Conflict,"Nested acquisition source observation");knownFamily(caller.directoryHandle(),"subscriptions.config",{beforeConfig},configBackup);knownFamily(child.get(),name,{beforeResults},resultsBackup);require(readFile(caller.directoryHandle(),"subscriptions.config").bytes==beforeConfig&&readFile(child.get(),name).bytes==beforeResults,storage::ErrorCode::Conflict,"Subscription changed before association preparation");}
      intent->when=nowMillis();intent->values=associationRead(beforeConfig,beforeResults,selected,plan->images().hash,*intent->when);persisted(caller);
    }
    {
      auto access=take(caller.sourceAccess(true));require(access.observationsAllowed(),storage::ErrorCode::Conflict,"Nested acquisition source publication");
      const auto progress=logicalProgress(caller.directoryHandle(),child.get(),name,beforeConfig,configBackup,beforeResults,resultsBackup,intent->values,intent->stopped,intent->done);
      const auto persistHere=[&]{publishIntent(caller.directoryHandle(),ownerBytes,*intent);ownerBytes=encodeIntent(*intent);};
      const auto revoked=[&]{return stopped->stop_requested()||!staticBindings(caller.directoryHandle(),child.get(),frozen.root());};
      const auto rollback=[&]{
        // Persist the refusal BEFORE rolling back any owned subscription value.
        // A crash during rollback must never forward this attempt later.
        intent->stopped=true;persistHere();
        writeValue(caller.directoryHandle(),"subscriptions.config",beforeConfig);writeValue(child.get(),name,beforeResults);
        require(readFile(caller.directoryHandle(),"subscriptions.config").bytes==beforeConfig&&readFile(child.get(),name).bytes==beforeResults,storage::ErrorCode::Conflict,"Acquisition rollback is incomplete");
      };
      if(intent->done){require(intent->values&&readFile(caller.directoryHandle(),"subscriptions.config").bytes==intent->values->finalConfiguration&&readFile(child.get(),name).bytes==intent->values->results,storage::ErrorCode::Conflict,"Completed acquisition values were externally changed");}
      else if(intent->stopped||stopBeforeValues||revoked()){rollback();}
      else{
        require(intent->values.has_value(),storage::ErrorCode::Conflict,"Acquisition postimages absent");
        if(progress==0){writeValue(caller.directoryHandle(),"subscriptions.config",intent->values->association);crashCheckpoint("AssociationPublished");}
        if(revoked()){rollback();}else{
          if(progress<=1){writeValue(child.get(),name,intent->values->results);crashCheckpoint("ReadPublished");}
          if(revoked()){rollback();}else{
            if(progress<=2){writeValue(caller.directoryHandle(),"subscriptions.config",intent->values->finalConfiguration);crashCheckpoint("FinalPublished");}
            if(revoked()){rollback();}else{
              require(readFile(caller.directoryHandle(),"subscriptions.config").bytes==intent->values->finalConfiguration&&readFile(child.get(),name).bytes==intent->values->results,storage::ErrorCode::Conflict,"Acquisition final postimages changed");
              intent->done=true;persistHere();crashCheckpoint("Done"); // Durable linearization point.
            }
          }
        }
      }
      result.associated=intent->done;result.read=intent->done;result.cancelled=intent->stopped&&stopped->stop_requested();
      if(intent->stopped){result.warning=QStringLiteral("Torrent saved stopped; the subscription result remains unread.");}
      // Final proof, Done publication and acknowledgement share ONE gate. A
      // different genuine writer cannot intervene before this result is sealed.
      cleanupTemporary(*intent);tempDirectory.clear();removeIntent(caller.directoryHandle(),ownerBytes);
    }
    intent.reset();ownerBytes.clear();result.needsReview=false;terminal=true;ready=false;return result;
  }
};

SavedSubscriptionAcquisition::SavedSubscriptionAcquisition(std::unique_ptr<Impl> value):impl_(std::move(value)){}
SavedSubscriptionAcquisition::~SavedSubscriptionAcquisition(){if(impl_){try{(void)close();}catch(...){}}}
std::expected<std::unique_ptr<SavedSubscriptionAcquisition>,storage::Error> SavedSubscriptionAcquisition::begin(const storage::ProfileLease& caller,std::filesystem::path profile,std::uint64_t session,search::SavedSubscriptionResultSelector selected,std::filesystem::path destination,std::stop_token receiver,Factory factory){try{
  require(session!=0&&!receiver.stop_requested(),storage::ErrorCode::Cancelled,"Acquisition was cancelled");profileCurrent(caller,profile);take(storage::requireNoPendingSubscriptionAcquisition(caller));
  std::error_code error;require(destination.is_absolute()&&std::filesystem::canonical(destination,error)==destination&&!error,storage::ErrorCode::UnsafePath,"Choose an existing canonical save directory");auto ownedDestination=checkedDirectory(destination);(void)ownedDestination;
  auto source=search::SavedSubscriptionResultSnapshot::read(caller,profile,session,selected);require(bool(source),storage::ErrorCode::Unsupported,"Saved subscription result is not supported for acquisition");
  auto value=std::make_unique<Impl>();value->lease=take(caller.retain());value->profile=std::move(profile);value->destination=std::move(destination);value->session=session;value->selected=std::move(selected);value->source.emplace(std::move(*source));
  const auto token=value->stopped->get_token();if(factory){value->fetch=factory(caller,value->profile,session,value->selected,token);}else{auto fetch=search::SubscriptionMetainfoFetch::openOneShot(caller,value->profile,session,value->selected.subscription,value->selected.resultKey,token);require(bool(fetch),storage::ErrorCode::Unsupported,"Metainfo fetch could not be opened");value->fetch=std::move(*fetch);}
  require(bool(value->fetch),storage::ErrorCode::Conflict,"Metainfo factory returned no actual owner");const auto observation=value->fetch->observation();
  require(value->fetch->matchesSubscriptionResultSource(caller,value->profile,session,value->selected.subscription,value->selected.resultKey,token)&&observation.phase==search::RssFetchPhase::BeforeDNS&&!observation.gets&&!observation.resolverSubmissions&&!observation.failure&&value->source->current(caller,session).has_value(),storage::ErrorCode::Conflict,"Metainfo owner does not match this saved result and receiver");
  std::weak_ptr<std::stop_source> weak=value->stopped;value->receiver.emplace(receiver,[weak]{if(auto live=weak.lock()){live->request_stop();}});
  return std::unique_ptr<SavedSubscriptionAcquisition>(new SavedSubscriptionAcquisition(std::move(value)));
}catch(const AcquisitionFailure& error){return std::unexpected(error.error);}catch(const std::bad_alloc&){return std::unexpected(storage::Error{storage::ErrorCode::Io,"Acquisition allocation failed"});}}
std::expected<std::optional<SavedSubscriptionAcquisition::Outcome>,storage::Error> SavedSubscriptionAcquisition::poll(storage::ProfileLease& caller,std::uint64_t session){try{
  impl_->guard(caller,session);if(impl_->terminal){return std::optional{impl_->result};}if(impl_->ready){return std::optional<Outcome>{};}
  if(impl_->stopped->stop_requested()){impl_->closeFetch();impl_->cleanupTemp();impl_->result.cancelled=true;impl_->terminal=true;return std::optional{impl_->result};}
  require(impl_->source&&impl_->source->current(caller,session).has_value(),storage::ErrorCode::Conflict,"Saved result changed while acquiring metainfo");
  auto response=impl_->fetch->poll(caller,session);if(!response){impl_->closeFetch();return std::unexpected(storage::Error{response.error()==search::RssFetchError::Cancelled?storage::ErrorCode::Cancelled:storage::ErrorCode::Io,"Metainfo fetch failed"});}if(!*response){return std::optional<Outcome>{};}
  const auto observation=impl_->fetch->observation();require(observation.joined&&(*response)->session==session&&(*response)->nonce==observation.nonce,storage::ErrorCode::Conflict,"Metainfo completion identity changed");impl_->closeFetch();
  require(!impl_->stopped->stop_requested()&&impl_->source->current(caller,session).has_value(),storage::ErrorCode::Cancelled,"Acquisition cancelled before metadata inspection");
  std::string pattern="/tmp/biglybtc-sub-acq-XXXXXX";require(::mkdtemp(pattern.data())!=nullptr,storage::ErrorCode::Io,"Private acquisition directory could not be created");impl_->tempDirectory=pattern;auto directory=checkedDirectory(pattern);struct stat st{};require(::fstat(directory.get(),&st)==0,storage::ErrorCode::Io,"Private acquisition directory changed");impl_->tempScope=scope(st);const auto& bytes=(*response)->bytes;std::string metainfo(bytes.constData(),static_cast<std::size_t>(bytes.size()));impl_->tempHash=digest(metainfo);writeNew(directory.get(),"metadata.torrent",metainfo);synchronize(directory.get());
  auto context=take(storage::readOriginalFilenameContext(caller));auto metadata=take(storage::inspectTorrentIntake(std::filesystem::path(pattern)/"metadata.torrent",impl_->stopped->get_token(),context.conversions,context.decoderShowAll));
  const auto expected=impl_->source->description().result.expectedV1Hash;if(expected){require(metadata.hash().bytes().size()==expected->size()&&std::equal(expected->begin(),expected->end(),metadata.hash().bytes().begin()),storage::ErrorCode::Conflict,"Fetched metainfo does not match the saved result hash");}
  impl_->plan.emplace(take(storage::PreparedStoppedPublicIntake::prepare(caller,metadata,impl_->destination,impl_->source->description().result.initialScrapeCache,impl_->stopped->get_token())));impl_->hash=impl_->plan->images().hash;
  require(impl_->source->current(caller,session).has_value()&&!impl_->stopped->stop_requested(),storage::ErrorCode::Cancelled,"Acquisition cancelled before intake preparation");impl_->ready=true;return std::optional<Outcome>{};
}catch(const AcquisitionFailure& error){impl_->closeFetch();impl_->result.needsReview=!impl_->ownerBytes.empty();return std::unexpected(error.error);}catch(const std::bad_alloc&){return std::unexpected(storage::Error{storage::ErrorCode::Io,"Acquisition allocation failed"});}}
bool SavedSubscriptionAcquisition::readyForIntake()const noexcept{return impl_&&!impl_->closed&&impl_->ready&&!impl_->terminal;}
const SavedSubscriptionAcquisition::Outcome& SavedSubscriptionAcquisition::outcome()const noexcept{return impl_->result;}
const core::TorrentHash& SavedSubscriptionAcquisition::intakeHash()const noexcept{return *impl_->hash;}
std::expected<void,storage::Error> SavedSubscriptionAcquisition::saveIntake(storage::ProfileLease& caller,std::uint64_t session,storage::IntakeHistoryOutcome* history){try{
  impl_->guard(caller,session);require(impl_->ready&&impl_->plan&&impl_->source&&!impl_->intent,storage::ErrorCode::Conflict,"Acquisition is not ready for stopped intake");require(!impl_->stopped->stop_requested(),storage::ErrorCode::Cancelled,"Acquisition cancelled before intake");
  auto source=impl_->source->freezeAcquisitionSource(caller,session);require(bool(source),storage::ErrorCode::Conflict,"Acquisition source changed before intake");
  Intent intent;intent.source=std::move(*source);intent.selection=selectedBytes(impl_->selected);intent.plan=impl_->plan->frozenJournal();intent.nonce=std::to_string(QRandomGenerator::system()->generate64());intent.tempDirectory=impl_->tempDirectory;intent.tempScope=impl_->tempScope;intent.tempHash=impl_->tempHash;impl_->intent=std::move(intent);
  // The owner marker is published only here, inside Host::publishTarget.
  std::expected<core::TorrentHash,storage::Error> committed=std::unexpected(storage::Error{storage::ErrorCode::Conflict,"Stopped intake has not committed"});
  {auto frozen=document(impl_->intent->source);auto child=sourceDirectory(caller,frozen.root());auto access=take(caller.sourceAccess(true));require(access.observationsAllowed()&&staticBindings(caller.directoryHandle(),child.get(),frozen.root()),storage::ErrorCode::Conflict,"Source changed at stopped intake publication");require(contents(readFile(caller.directoryHandle(),"subscriptions.config"))==optionalField(frozen.root(),"configuration")&&contents(readFile(child.get(),resultsName(impl_->selected)))==optionalField(frozen.root(),"results"),storage::ErrorCode::Conflict,"Acquisition result changed before intake");publishIntent(caller.directoryHandle(),{},*impl_->intent);impl_->ownerBytes=encodeIntent(*impl_->intent);impl_->result.needsReview=true;
    // No observer/network/parser/UI runs under this gate. The private raw-FD
    // publisher consumes exactly the storage-built plan frozen above.
    crashCheckpoint("BeforeIntake");
#if defined(BIGLYBTC_RSS_FETCH_TEST_FIXTURE)
    if(const char* fault=std::getenv("BIGLYBTC_SUBSCRIPTION_ACQUISITION_CRASH_AT");fault&&*fault){committed=impl_->plan->commit(caller,impl_->ownerBytes,impl_->stopped->get_token(),crashIntakeStage,nullptr);}else
#endif
    {committed=impl_->plan->commit(caller,impl_->ownerBytes,impl_->stopped->get_token(),nullptr,history);}
  }
  if(!committed){
    const auto state=take(impl_->plan->inspect(caller,impl_->ownerBytes));if(state==storage::PreparedStoppedPublicIntake::State::Committed){impl_->result.imported=true;impl_->result.hash=impl_->plan->images().hash;}
    if(state==storage::PreparedStoppedPublicIntake::State::Before){cleanupTemporary(*impl_->intent);impl_->tempDirectory.clear();auto access=take(caller.sourceAccess(true));require(access.observationsAllowed(),storage::ErrorCode::Conflict,"Nested acquisition abort");removeIntent(caller.directoryHandle(),impl_->ownerBytes);impl_->intent.reset();impl_->ownerBytes.clear();impl_->result.needsReview=false;}
    return std::unexpected(committed.error());
  }
  require(take(impl_->plan->inspect(caller,impl_->ownerBytes))==storage::PreparedStoppedPublicIntake::State::Committed,storage::ErrorCode::Conflict,"Stopped intake postimages are incomplete");impl_->result.imported=true;impl_->result.hash=*committed;impl_->hash=*committed;return {};
}catch(const AcquisitionFailure& error){impl_->result.needsReview=!impl_->ownerBytes.empty();return std::unexpected(error.error);}catch(const std::bad_alloc&){impl_->result.needsReview=!impl_->ownerBytes.empty();return std::unexpected(storage::Error{storage::ErrorCode::Io,"Acquisition intent allocation failed"});}}
std::expected<SavedSubscriptionAcquisition::Outcome,storage::Error> SavedSubscriptionAcquisition::finishSubscription(storage::ProfileLease& caller,std::uint64_t session){try{impl_->guard(caller,session);require(impl_->intent.has_value(),storage::ErrorCode::Conflict,"Acquisition intake has no owned intent");const bool current=impl_->source&&impl_->source->current(caller,session).has_value();return impl_->settle(caller,current);}catch(const AcquisitionFailure& error){impl_->result.needsReview=!impl_->ownerBytes.empty();return std::unexpected(error.error);}catch(const std::bad_alloc&){impl_->result.needsReview=true;return std::unexpected(storage::Error{storage::ErrorCode::Io,"Acquisition completion allocation failed"});}}
void SavedSubscriptionAcquisition::cancel()noexcept{if(impl_){impl_->stopped->request_stop();}}
std::expected<void,storage::Error> SavedSubscriptionAcquisition::close(){
  if(!impl_||impl_->closed){return {};}
  if(impl_->worker!=std::this_thread::get_id()){return std::unexpected(storage::Error{storage::ErrorCode::Conflict,"Acquisition close requires its creator worker"});}
  cancel();impl_->closeFetch();std::optional<storage::Error> failure;
  try{if(impl_->ownerBytes.empty()){impl_->cleanupTemp();}else{failure=storage::Error{storage::ErrorCode::Conflict,"Owned acquisition remains pending review or recovery"};}}
  catch(const AcquisitionFailure& error){failure=error.error;}
  catch(const std::bad_alloc&){failure=storage::Error{storage::ErrorCode::Io,"Acquisition close allocation failed"};}
  // Closing always releases retained source/OFD aliases, even while this closed
  // wrapper remains alive. Unknown owned temporary data and intents are kept.
  impl_->source.reset();impl_->plan.reset();impl_->receiver.reset();impl_->lease.reset();impl_->closed=true;impl_->result.needsReview=!impl_->ownerBytes.empty();
  if(failure){return std::unexpected(std::move(*failure));}return {};
}
std::expected<std::optional<SavedSubscriptionAcquisition::Outcome>,storage::Error> SavedSubscriptionAcquisition::recover(storage::ProfileLease& caller,const std::filesystem::path& profile){try{
  profileCurrent(caller,profile);auto primary=readFile(caller.directoryHandle(),std::string(OwnerName)),saving=readFile(caller.directoryHandle(),std::string(OwnerName)+".saving"),backup=readFile(caller.directoryHandle(),std::string(OwnerName)+".bak");require(!backup.present,storage::ErrorCode::Conflict,"Unknown acquisition backup requires review");if(!primary.present&&!saving.present){return std::optional<Outcome>{};}
  std::optional<Intent> previous;if(primary.present){previous=parseIntent(primary.bytes);}auto intent=saving.present?parseIntent(saving.bytes):*previous;if(previous&&saving.present){require(extends(*previous,intent),storage::ErrorCode::Conflict,"Acquisition saving is not a monotone owned state");}
  auto value=std::make_unique<Impl>();value->profile=profile;value->selected=selectedFrom(intent.selection);value->intent=std::move(intent);value->plan.emplace(take(storage::PreparedStoppedPublicIntake::restore(caller,value->intent->plan)));auto frozen=document(value->intent->source);value->validateValues(frozen.root());auto child=sourceDirectory(caller,frozen.root());(void)child;
  if(saving.present){auto access=take(caller.sourceAccess(true));require(access.observationsAllowed()&&named(caller.directoryHandle(),primary)&&named(caller.directoryHandle(),saving),storage::ErrorCode::Conflict,"Acquisition owner changed during recovery");if(primary.present){require(::renameat(caller.directoryHandle(),(std::string(OwnerName)+".saving").c_str(),caller.directoryHandle(),std::string(OwnerName).c_str())==0,storage::ErrorCode::Io,"Acquisition owner recovery failed");}else{require(::syscall(SYS_renameat2,caller.directoryHandle(),(std::string(OwnerName)+".saving").c_str(),caller.directoryHandle(),std::string(OwnerName).c_str(),RENAME_NOREPLACE)==0,storage::ErrorCode::Conflict,"Acquisition initial owner recovery failed");}synchronize(caller.directoryHandle());}
  value->ownerBytes=encodeIntent(*value->intent);const auto state=take(value->plan->inspect(caller,value->ownerBytes));if(state==storage::PreparedStoppedPublicIntake::State::Before){require(!value->intent->values&&!value->intent->done,storage::ErrorCode::Conflict,"Unimported acquisition has committed subscription values");cleanupTemporary(*value->intent);auto access=take(caller.sourceAccess(true));require(access.observationsAllowed(),storage::ErrorCode::Conflict,"Nested acquisition abort");removeIntent(caller.directoryHandle(),value->ownerBytes);Outcome outcome;outcome.cancelled=true;return std::optional{outcome};}
  if(state==storage::PreparedStoppedPublicIntake::State::Pending){take(value->plan->commit(caller,value->ownerBytes));}
  // No receiver, source policy grant, or history receipt is reconstructed.
  // Exact frozen source/preimages decide only this already-owned attempt.
  return std::optional{value->settle(caller,true)};
}catch(const AcquisitionFailure& error){return std::unexpected(error.error);}catch(const std::bad_alloc&){return std::unexpected(storage::Error{storage::ErrorCode::Io,"Acquisition recovery allocation failed"});}}
} // namespace biglybtc::app
