#include "original_filename_policy.hpp"
#include "torrent_intake.hpp"
#include "verified_seeding.hpp"
#include "file_operations.hpp"
#include "linear_to_compact.hpp"
#include "original_storage_defaults.hpp"
#include "torrent_creator.hpp"
#include <thread>
#include "magnet_intake.hpp"
#include "file_policy_commands.hpp"
#include "library_commands.hpp"
#include "network_policy.hpp"
#include "session_bridge.hpp"
#include "application_settings.hpp"
#include "scheduler_service.hpp"
#include "ownership_file.hpp"
#include "torrent_state.hpp"
#include "../adapters/local_rating.hpp"
#include <QDateTime>
#include "../engine/live_settings.hpp"
#include "owned_azureus_service.hpp"
#include "owned_dht_accounting.hpp"
#include "live_speed_mapper_binding.hpp"

#include "library_view.hpp"
#include "column_layout_view.hpp"
#include "biglybtc/saved_dnd.hpp"
#include "local_session.hpp"
#include "persistent_session.hpp"
#include "overall_statistics_service.hpp"
#include "source_rate_projection.hpp"
#include "minute_history_writer.hpp"
#include "saved_service_view.hpp"
#include "statistics_view.hpp"
#include "saved_download_tags_view.hpp"
#include "country_provider.hpp"
#include "../adapters/saved_speed_manager_view.hpp"
#include "../adapters/history_view.hpp"
#include "../adapters/history_week_view.hpp"
#include "../adapters/history_month_view.hpp"
#include "../adapters/history_range_view.hpp"

#include <QMetaObject>
#include <QPointer>
#include <QTimer>

#include <limits>
#include <bit>
#include <chrono>
#include <algorithm>
#include <array>
#include <QStringList>
#include <map>
#include <set>
#include <tuple>
#include <stdexcept>

namespace biglybtc::app {
namespace {
struct Failure { storage::Error error; };
std::int64_t steadyMillis() noexcept {
  return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
std::int64_t wallMillis() noexcept {
  return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}
OverallOwnerIdentity ownerIdentity(const engine::AccountingIdentity& value) noexcept {
  return {value.applicationRun, value.profileSession, value.engineEpoch};
}
core::OverallPeerCounters peerCounters(const core::EngineCounters& value) noexcept {
  return {value.payloadReceived, value.received - value.payloadReceived,
      value.payloadSent, value.sent - value.payloadSent};
}
core::HistoryCounters historyPeer(const core::EngineCounters& value) noexcept {
  return {value.sent - value.payloadSent, value.payloadSent, value.received - value.payloadReceived,
      value.payloadReceived, 0, 0}; // Caller fills the independent owned DHT source.
}
core::MinuteHistoryInput historyInput(const core::EngineCounters& value,
    std::optional<std::array<std::int64_t, 2>> dht) noexcept {
  const auto v = historyPeer(value);
  return {std::array{v[0], v[1], v[2], v[3]}, dht};
}
ui::NativeSessionPresentation nativePacket(const engine::ApplicationPeerObservation& value,
    std::optional<core::SourceRateValues> rates = {}) {
  ui::NativeSessionState state;
  switch (value.state) {
    case engine::AccountingState::Starting: state = ui::NativeSessionState::Starting; break;
    case engine::AccountingState::Idle: state = ui::NativeSessionState::Idle; break;
    case engine::AccountingState::Active: state = ui::NativeSessionState::Active; break;
    case engine::AccountingState::Retiring: state = ui::NativeSessionState::Retiring; break;
    case engine::AccountingState::Closing: state = ui::NativeSessionState::Closing; break;
    case engine::AccountingState::Closed: state = ui::NativeSessionState::Closed; break;
    case engine::AccountingState::Uncertified: state = ui::NativeSessionState::Uncertified; break;
    default: return {};
  }
  return std::make_shared<const ui::NativeSessionPacket>(ui::NativeSessionPacket{
      value.identity.profileSession, value.identity.applicationRun, value.identity.engineEpoch,
      value.sequence, state, value.observed, value.ownerElapsedNanoseconds, std::move(rates), value.rateLimits});
}
quint64 reserveColumnLayoutSession() {
  static std::atomic<quint64> last{0};
  auto previous = last.load(std::memory_order_relaxed);
  for (;;) {
    if (previous == std::numeric_limits<quint64>::max()) {
      throw Failure{{storage::ErrorCode::Unsupported, "Column layout session identities are exhausted"}};
    }
    if (last.compare_exchange_weak(previous, previous + 1, std::memory_order_relaxed)) { return previous + 1; }
  }
}
quint64 reserveLibraryRevision() {
  static std::atomic<quint64> last{0};
  auto previous = last.load(std::memory_order_relaxed);
  for (;;) {
    if (previous == std::numeric_limits<quint64>::max()) {
      throw Failure{{storage::ErrorCode::Unsupported, "Library revision identities are exhausted"}};
    }
    if (last.compare_exchange_weak(previous, previous + 1, std::memory_order_relaxed)) { return previous + 1; }
  }
}
template<typename T> T take(std::expected<T, storage::Error> value) {
  if (!value) { throw Failure{value.error()}; }
  return std::move(*value);
}
void checkpoint(std::stop_token stop) {
  if (stop.stop_requested()) { throw Failure{{storage::ErrorCode::Cancelled, "Cancelled"}}; }
}
QString text(std::string_view value) { return QString::fromUtf8(value.data(), static_cast<qsizetype>(std::min(value.size(), std::size_t{8192}))); }
std::optional<ui::SavedPageSource> savedSource(storage::RecoverySource source) {
  switch (source) {
    case storage::RecoverySource::Primary: return ui::SavedPageSource::Primary;
    case storage::RecoverySource::Backup: return ui::SavedPageSource::Backup;
    case storage::RecoverySource::Saving: return ui::SavedPageSource::Saving;
  }
  return std::nullopt;
}
std::optional<ui::ColumnLayoutReadSource> columnLayoutSource(storage::RecoverySource source) {
  switch (source) {
    case storage::RecoverySource::Primary: return ui::ColumnLayoutReadSource::Primary;
    case storage::RecoverySource::Backup: return ui::ColumnLayoutReadSource::Backup;
    case storage::RecoverySource::Saving: return ui::ColumnLayoutReadSource::Saving;
  }
  return std::nullopt;
}
template<typename Snapshot, typename View>
ui::SavedServiceResult<Snapshot> savedResult(std::expected<View, storage::Error> view) {
  ui::SavedServiceResult<Snapshot> result;
  if (!view) {
    if (view.error().code == storage::ErrorCode::Cancelled) { throw Failure{std::move(view.error())}; }
    result.failure = text(view.error().message);
    return result;
  }
  if (view->missing()) {
    result.state = ui::SavedPageState::Missing;
    return result;
  }
  if (!view->recoverySource || !(result.source = savedSource(*view->recoverySource))) {
    result.failure = SessionBridge::tr("Saved configuration source is unavailable");
    return result;
  }
  result.snapshot = std::make_shared<const Snapshot>(std::move(*view->snapshot));
  result.state = ui::SavedPageState::Ready;
  return result;
}
QString managedState(engine::ManagedPhase state) {
  switch (state) {
    case engine::ManagedPhase::Checking: return SessionBridge::tr("Checking");
    case engine::ManagedPhase::Queued: return SessionBridge::tr("Queued");
    case engine::ManagedPhase::Downloading: return SessionBridge::tr("Downloading");
    case engine::ManagedPhase::Seeding: return SessionBridge::tr("Seeding");
    case engine::ManagedPhase::Paused: return SessionBridge::tr("Paused");
    case engine::ManagedPhase::Stopping: return SessionBridge::tr("Stopping");
    case engine::ManagedPhase::Stopped: return SessionBridge::tr("Stopped");
    case engine::ManagedPhase::Failed: return SessionBridge::tr("Error");
  }
  return {};
}
constexpr quint16 actionBit(core::DownloadAction value) {
  const auto ordinal = static_cast<unsigned int>(value);
  return ordinal < 10 ? static_cast<quint16>(1U << ordinal) : quint16{0};
}
core::TorrentHash identity(const ui::Target& target) {
  const auto hex = target.hash.toLatin1();
  const auto bytes = QByteArray::fromHex(hex);
  if (bytes.toHex().toUpper() != hex) { throw Failure{{storage::ErrorCode::InvalidFormat, "Invalid download identity"}}; }
  const auto value = core::TorrentHash::parse(std::string_view(bytes.data(), static_cast<std::size_t>(bytes.size())));
  if (!value) { throw Failure{{storage::ErrorCode::InvalidFormat, "Invalid download identity"}}; }
  return *value;
}
}

namespace {
void take(std::expected<void, storage::Error> value) {
  if (!value) { throw Failure{std::move(value.error())}; }
}
}
struct SessionBridge::Host {
  struct Known { QString name; std::optional<qint64> bytes{}; };
  std::filesystem::path profile;
  unsigned short peerPort;
  bool normalNetwork = false;
  engine::NetworkPolicy networkPolicy{}; // Isolated until normal configure succeeds.
  bool socks4CapabilityChecked = false; // Worker-owned, independent of config repair success.
  using MagnetStamp = std::tuple<std::uint64_t, engine::MagnetPhase, bool, std::string,
      std::optional<storage::ErrorCode>, std::string>;
  std::map<std::string, MagnetStamp> lastMagnetPhases;
  std::map<std::string, engine::PendingMagnet> pendingIntents;
  std::set<std::string> pendingTargets;
  std::optional<storage::Error> pendingReadFailure;
  bool pendingSourceCurrent = false;
  std::map<std::string, std::uint64_t> reportedMagnetAdmissions;
  using ManagedStamp = std::tuple<engine::ManagedPhase, std::optional<core::QueueReason>,
      bool, std::uint32_t, bool>;
  std::map<std::string, ManagedStamp> lastManagedPhases;
  std::map<std::string, storage::Error> admissionFailures;
  std::vector<storage::LibraryCompletionPool> movePools;
  storage::ProfileLease lease;
  std::optional<storage::TorrentIntakeMetadata> intakeMetadata;
  std::optional<ui::TorrentIntakeRequest> intakeRequest;
  quint64 intakePreviewRevision = 0;
  // Declared after lease: affine engine destruction/join precedes lease release.
  std::unique_ptr<engine::PersistentSession> engine{};
  std::unique_ptr<OwnedAzureusService> azureus{};
  bool azureusEnabled = false, azureusPublished = false;
  std::optional<OwnedDhtAccounting> dhtAccounting{};
  quint64 azureusSequence = 0, azureusObservationSequence = 0;
  const quint64 columnLayoutSession;
  // All services are worker-owned; the held lease outlives their destruction.
  std::unique_ptr<OverallStatisticsService> overall{};
  std::unique_ptr<SourceRateProjection> sourceRates{};
  std::unique_ptr<LiveSpeedMapperBinding> liveSpeedMapper{};
  std::uint64_t liveSpeedRevision = 0, liveSpeedSequence = 0;
  bool liveSpeedPublished = false;
  std::optional<storage::Error> overallOpenFailure{};
  core::MinuteHistoryRecorder minuteHistory{};
  std::optional<storage::Error> historyFailure{};
  bool historyEnabled = true;
  quint64 historyRevision = 0;
  std::optional<core::EngineStatistics> lastCounterSample{};
  std::chrono::steady_clock::time_point lastCounterArrival{};
  std::chrono::steady_clock::time_point lastAccountingTick = std::chrono::steady_clock::now();
  std::optional<core::SourceRateValues> latestSourceRates{};
  quint64 overallSequence = 0;
  std::chrono::steady_clock::time_point lastOverallPublication{};
  quint64 revision = 0;
  quint64 engineOperation = 0;
  std::map<std::string, Known> known;
  std::set<std::string> currentHashes;
  // Optional manual-tag cache is worker-only and never loaded by startup.
  bool manualTagsEnabled = false, manualTagCommandActive = false, manualTagEnded = false;
  quint64 manualTagRevision = 0, manualTagSequence = 0;
  quint64 manualResolvedLibrary = 0, manualResolvedTag = 0;
  ui::ManualTagReadState manualTagReadState = ui::ManualTagReadState::Failed;
  QString manualTagFailure, manualJoinFailure;
  std::shared_ptr<const core::SavedDownloadTags> manualTagSource;
  std::shared_ptr<const core::ManualTagMembership> manualMembership;
  quint64 manualOwnerEpoch = 0, manualOwnerSequence = 0;
  engine::AccountingState manualOwnerState = engine::AccountingState::Starting;
  bool manualOwnerSampled = false, manualIdleAuthority = false;
  std::optional<core::TargetTraffic> manualTarget;
  core::ManualTagTraffic manualLastTraffic{};
  // Optional geography is never initialized by profile startup or transfers.
  bool countriesEnabled = false, countryProviderAttempted = false, countryEnded = false;
  quint64 countrySequence = 0;
  std::unique_ptr<storage::CountryProvider> countryProvider;
  std::shared_ptr<const core::CountryStatistics> lastCountries;
  ui::Presentation lastSnapshot{};
  std::shared_ptr<const core::Document> columnBaseline{};
  std::optional<storage::RecoverySource> columnBaselineSource{};
  bool columnBaselineMissing = false;
  std::stop_token columnBaselineReadStop{};

  std::optional<storage::ApplicationSettingsSnapshot> optionsBaseline;
  quint64 optionsRevision = 0;
  std::stop_token optionsReadStop;
  QString intakeDefault;
  bool intakeDefaultLoaded = false;

  std::optional<ui::LocalRatingRequest> ratingRequest;
  std::array<std::uint8_t,32> ratingRevision{};
  std::optional<storage::PreparedFileOperation> filePlan;
  ui::FileOperationPlanPresentation filePlanPacket;
  bool fileRecoveryRequired = false;
  bool replacementReviewOnly = false; // No engine/services/admissions until explicit journal review succeeds.
  std::unique_ptr<storage::SchedulerService> scheduler;
  bool schedulerRestoreDefaults=false;
  bool schedulerAttempted=false, schedulerTagsLoaded=false, schedulerTagsAvailable=false;
  std::optional<std::chrono::steady_clock::time_point> schedulerLastTick;
  QString schedulerFailure, schedulerPublishedFailure;
  std::map<std::string,std::uint64_t> manualRevisions;
  std::set<std::string> schedulerPaused;
  bool verifiedSeedIntentsLoaded=false;
  std::set<std::string> verifiedSeedPending;
  void loadVerifiedSeedIntents() {
    if(verifiedSeedIntentsLoaded){return;}
    const auto file=take(storage::ConfigFile::load(lease,"downloads.config"));
    if(const auto* rows=file.document().find("downloads");rows&&rows->type==core::Node::Type::List){
      for(const auto& row:rows->children){if(const auto* marker=row.find(storage::VerifiedSeedKey)){
        const auto* id=row.find("torrent_hash");
        if(marker->type!=core::Node::Type::Integer||(marker->integer!=1&&marker->integer!=2)
            ||!id||id->type!=core::Node::Type::Bytes){throw Failure{{storage::ErrorCode::InvalidFormat,"Invalid saved verified-seeding intent"}};}
        const auto hash=core::TorrentHash::parse(id->bytes);if(!hash){throw Failure{{storage::ErrorCode::InvalidFormat,"Invalid verified-seeding identity"}};}
        verifiedSeedPending.insert(hash->hex());
      }}
    }
    verifiedSeedIntentsLoaded=true;
  }
  bool verifiedSeedBlocked(const std::string& key,const std::vector<engine::ManagedDownloadStatus>& active) {
    loadVerifiedSeedIntents();
    for(const auto& item:active){if(item.hash.hex()==key&&item.verifiedSeeding!=engine::VerifiedSeedingPhase::None){
      if(item.verifiedSeeding==engine::VerifiedSeedingPhase::Released&&!item.failure&&item.retained){verifiedSeedPending.erase(key);}
      else{verifiedSeedPending.insert(key);}
      return item.verificationHeld||verifiedSeedPending.contains(key);
    }}
    return verifiedSeedPending.contains(key);
  }
  struct SchedulerMembership { bool available=false; std::vector<std::string> names; };
  std::map<std::string,SchedulerMembership> schedulerMemberships;
  SchedulerMembership schedulerMembership(const core::TorrentHash& hash) {
    const auto key=hash.hex();
    if(const auto known=schedulerMemberships.find(key);known!=schedulerMemberships.end()){
      auto cached=known->second;
      if(scheduler->snapshot().settings.useTags){cached.available=cached.available&&schedulerTagsLoaded&&schedulerTagsAvailable;}
      return cached;
    }
    SchedulerMembership result;
    if(scheduler->snapshot().settings.useTags){
      if(!schedulerTagsLoaded){
        schedulerTagsLoaded=true;
        auto source=storage::readSavedDownloadTagsView(lease);
        if(source){
          schedulerTagsAvailable=true;
          if(source->snapshot){
            for(const auto& tag:source->snapshot->tags){
              if(!tag.rawName||tag.nameMayRequireLocalization||tag.duplicateId||tag.membersMalformed){schedulerTagsAvailable=false;break;}
              for(const auto& member:tag.uniqueHashes){auto& row=schedulerMemberships[member.hex()];row.available=true;row.names.push_back(*tag.rawName);}
            }
            if(!source->snapshot->issues.empty()){schedulerTagsAvailable=false;}
          }
        }
      }
      if(const auto tag=schedulerMemberships.find(key);tag!=schedulerMemberships.end()){result=tag->second;}
      result.available=schedulerTagsAvailable;
    }else{
      auto store=storage::TorrentStateStore::open(lease);
      if(store){
        auto state=store->load(lease,hash);
        if(state){
          result.available=true;result.names={"Uncategorized"};
          if(const auto* attributes=state->document().find("attributes")){
            if(attributes->type!=core::Node::Type::Dictionary){result.available=false;}
            if(const auto* category=attributes->find("category")){
              if(category->type==core::Node::Type::Bytes&&category->bytes.size()<=4096){result.names={std::string(category->bytes)};}
              else{result.available=false;}
            }
          }
        }
      }
    }
    schedulerMemberships.insert_or_assign(key,result);return result;
  }
  std::vector<storage::ImportedSchedulerTarget> schedulerImportedTargets() {
    const auto source=take(storage::readLibraryView(lease));
    const auto active=engine?engine->managedDownloads():std::vector<engine::ManagedDownloadStatus>{};
    std::map<std::string,const engine::ManagedDownloadStatus*> live;
    for(const auto& row:active){if(row.retained){live.emplace(row.hash.hex(),&row);}}
    std::set<std::string> pauses,unique;
    for(const auto& pause:source.pauses){pauses.insert(pause.hash.hex());}
    const auto ownership=storage::scheduler::OwnershipFile::read(scheduler->snapshot().ownershipBytes);
    if(!ownership){throw Failure{{storage::ErrorCode::InvalidFormat,"Invalid imported Scheduler ownership"}};}
    const bool enabled=scheduler->snapshot().settings.enabled;
    const bool filters=enabled&&scheduler->snapshot().configuration&&std::ranges::any_of(
        scheduler->snapshot().configuration->schedules,[](const auto& rule){return rule.enabled&&(rule.onlyCategory||rule.excludeCategory);});
    std::vector<storage::ImportedSchedulerTarget> result;
    for(const auto& row:source.items){
      if(!row.saved.hash||!row.saved.persistent){continue;}
      const auto& hash=*row.saved.hash;const auto key=hash.hex();
      if(verifiedSeedBlocked(key,active)){continue;}
      if(!unique.insert(key).second){throw Failure{{storage::ErrorCode::Conflict,"Ambiguous imported Scheduler library target"}};}
      // A manual command may precede lazy module creation. Its current-Host
      // revocation remains authoritative even without a service to clean up.
      const auto manual=manualRevisions.find(key);
      if(manual!=manualRevisions.end()&&manual->second!=0){continue;}
      const auto raw=hash.bytes();const auto present=ownership->contains({reinterpret_cast<const std::uint8_t*>(raw.data()),raw.size()});
      if(!present){throw Failure{{storage::ErrorCode::InvalidFormat,"Invalid imported Scheduler hash"}};}
      if(!*present){continue;}
      storage::ImportedSchedulerTarget item;item.hash=hash;item.libraryRevision=revision;item.manualRevision=manualRevisions[key];
      item.forceStarted=row.saved.forceStart;item.paused=pauses.contains(key);
      item.state=row.saved.startup==core::StartupState::Stopped?storage::scheduler::TorrentState::Stopped
          :row.saved.startup==core::StartupState::Queued?storage::scheduler::TorrentState::Queued:storage::scheduler::TorrentState::Waiting;
      const auto found=live.find(key);
      if(found!=live.end()){
        const auto& current=*found->second;
        if(current.failure||current.phase==engine::ManagedPhase::Stopping){continue;}
        if(current.checked){item.complete=current.selectedComplete;}
        switch(current.phase){
          case engine::ManagedPhase::Checking:item.state=storage::scheduler::TorrentState::Preparing;break;
          case engine::ManagedPhase::Downloading:item.state=storage::scheduler::TorrentState::Downloading;break;
          case engine::ManagedPhase::Seeding:item.state=storage::scheduler::TorrentState::Seeding;break;
          case engine::ManagedPhase::Paused:case engine::ManagedPhase::Stopped:item.state=storage::scheduler::TorrentState::Stopped;break;
          default:item.state=storage::scheduler::TorrentState::Queued;break;
        }
      }else if(enabled){
        // The existing original no-DiskManager projection is a saved source,
        // distinct from an actual LT checked admission. No fake engine token.
        if(row.cached){item.complete=core::projectSavedDndProgress(row.cached->bytes,row.saved.completedBytes,
            row.cached->savedDnd).controllerNoDiskCompleteExcludingDnd;}
        if(!item.complete){
          const auto details=engine::queryTorrent(lease,hash);
          auto store=storage::TorrentStateStore::open(lease);
          if(details&&store){auto attrs=store->loadAttributes(lease,hash);
            if(attrs&&!attrs->recoveredBackup()){
              const auto facts=core::readSavedDndFacts(attrs->document().find("attributes"));
              item.complete=core::projectSavedDndProgress(details->bytes,row.saved.completedBytes,facts).controllerNoDiskCompleteExcludingDnd;
            }
          }
        }
      }
      if(filters){auto membership=schedulerMembership(hash);item.membershipsAvailable=membership.available;item.memberships=std::move(membership.names);}
      result.push_back(std::move(item));
    }
    return result;
  }
  std::vector<storage::SchedulerTorrent> schedulerTorrents() {
    const auto saved=take(storage::readLibraryView(lease));
    const auto active=engine?engine->managedDownloads():std::vector<engine::ManagedDownloadStatus>{};
    std::map<std::string,const engine::ManagedDownloadStatus*> live;
    for(const auto& row:active){if(row.retained){live.emplace(row.hash.hex(),&row);}}
    std::set<std::string> pauses,unique;
    for(const auto& row:saved.pauses){pauses.insert(row.hash.hex());}
    std::vector<storage::SchedulerTorrent> result;result.reserve(saved.items.size());
    const bool filters=scheduler->snapshot().configuration&&std::ranges::any_of(
        scheduler->snapshot().configuration->schedules,[](const auto& rule){return rule.enabled&&(rule.onlyCategory||rule.excludeCategory);});
    for(const auto& row:saved.items){
      if(!row.saved.hash||!row.saved.persistent){continue;}
      const auto& hash=*row.saved.hash;const auto key=hash.hex();
      if(verifiedSeedBlocked(key,active)){continue;}
      if(!unique.insert(key).second){throw Failure{{storage::ErrorCode::Conflict,"Ambiguous scheduler torrent identity"}};}
      const auto found=live.find(key);
      // No inferred completion or synthetic admission for saved-only/unchecked
      // rows. Scheduling effects require this owner's actual retained handle.
      if(found==live.end()||!found->second->checked||found->second->failure){continue;}
      storage::SchedulerTorrent item;item.hash=hash;item.manualRevision=manualRevisions[key];
      item.forceStarted=row.saved.forceStart;item.paused=pauses.contains(key);
      {
        const auto& current=*found->second;item.token=current.admission;item.complete=current.selectedComplete;
        switch(current.phase){
          case engine::ManagedPhase::Checking:item.state=storage::scheduler::TorrentState::Preparing;break;
          case engine::ManagedPhase::Downloading:item.state=storage::scheduler::TorrentState::Downloading;break;
          case engine::ManagedPhase::Seeding:item.state=storage::scheduler::TorrentState::Seeding;break;
          case engine::ManagedPhase::Stopping:item.state=storage::scheduler::TorrentState::Stopping;break;
          case engine::ManagedPhase::Failed:item.state=storage::scheduler::TorrentState::Error;break;
          case engine::ManagedPhase::Paused:case engine::ManagedPhase::Stopped:item.state=storage::scheduler::TorrentState::Stopped;break;
          default:item.state=storage::scheduler::TorrentState::Queued;break;
        }
      }
      if(filters){auto membership=schedulerMembership(hash);item.membershipsAvailable=membership.available;item.memberships=std::move(membership.names);}
      result.push_back(std::move(item));
    }
    return result;
  }

  Host(std::filesystem::path p, unsigned short port, storage::ProfileLease held, quint64 session)
      : profile(std::move(p)), peerPort(port), lease(std::move(held)), columnLayoutSession(session) {}
  void accepts(const ui::Target& target) const {
    if (target.generation != revision || !currentHashes.contains(target.hash.toStdString())) {
      throw Failure{{storage::ErrorCode::Conflict, "This download selection is stale; refresh the library"}};
    }
  }
  bool compactionReady() const {
    if (fileRecoveryRequired || storage::hasPendingFileOperation(lease)
        || storage::hasPendingOriginalCompaction(lease)) { return false; }
    if (!engine) { return true; } // Offline/unbound: no engine was created.
    // managedAvailable also rejects registered legacy calls and poisoned/closed
    // owners. An empty status vector alone is not a lifetime certificate.
    if (!engine->managedAvailable()) { return false; }
    const auto observed = engine->latest();
    if (!observed || observed->state != engine::AccountingState::Idle) { return false; }
    for (const auto& item : engine->managedDownloads()) {
      if (item.retained || item.failure || item.phase != engine::ManagedPhase::Stopped) { return false; }
    }
    for (const auto& item : engine->pendingMagnets()) {
      if (item.retained || item.failure || item.phase == engine::MagnetPhase::Resolving
          || item.phase == engine::MagnetPhase::Retiring) { return false; }
    }
    return true;
  }
  void readMagnets() {
    auto read = engine::readPendingMagnets(lease, networkPolicy);
    pendingSourceCurrent = read.has_value();
    if (!read) {
      pendingReadFailure = read.error(); // Keep previous rows, visibly nonactionable.
      return;
    }
    std::map<std::string, engine::PendingMagnet> fresh;
    for (auto& item : *read) { fresh.emplace(item.hash.hex(), std::move(item)); }
    pendingIntents = std::move(fresh);
    pendingReadFailure.reset();
  }
  QString pendingNotice() const {
    if (pendingReadFailure) {
      return SessionBridge::tr("Pending magnets could not be read: %1").arg(text(pendingReadFailure->message));
    }
    for (const auto& [hash, item] : pendingIntents) {
      (void)hash;
      if (!item.destination) {
        return SessionBridge::tr("A recovered magnet needs an explicit destination; its saved intent was retained");
      }
    }
    return {};
  }
  quint16 magnetActions(const engine::PendingMagnet* intent, const engine::PendingMagnetStatus* live) const {
    if (!engine || !engine->managedAvailable() || !pendingSourceCurrent || !intent) { return 0; }
    if (live && (live->phase == engine::MagnetPhase::Retiring
        || (live->retained && live->phase != engine::MagnetPhase::Resolving))) { return 0; }
    quint16 actions = actionBit(core::DownloadAction::Stop);
    if ((!live || (!live->retained && live->phase == engine::MagnetPhase::Failed)) && intent->destination) {
      actions |= actionBit(core::DownloadAction::Start);
    }
    return actions;
  }
  void bootstrapMagnets(std::stop_token stop) {
    readMagnets();
    if (!pendingSourceCurrent || !engine) { return; }
    const auto library = take(storage::readLibraryView(lease));
    // Copy before completion removes a persisted row. No new handle is admitted
    // for a crash-recovered magnet already present in the ordinary inventory.
    const auto pending = pendingIntents;
    for (const auto& [hash, intent] : pending) {
      checkpoint(stop);
      const storage::LibraryItem* existing = nullptr;
      bool duplicate = false;
      for (const auto& item : library.items) {
        if (item.saved.hash && *item.saved.hash == intent.hash) {
          if (existing) { duplicate = true; }
          existing = &item;
        }
      }
      std::expected<void, storage::Error> admitted;
      if (duplicate) {
        admitted = std::unexpected(storage::Error{storage::ErrorCode::Conflict, "Duplicate ordinary magnet identity"});
      } else if (existing) {
        auto completed = engine::completePendingMagnet(lease, intent, existing->saved.torrentFile, networkPolicy);
        if (!completed) { admitted = std::unexpected(completed.error()); }
      } else if (!intent.destination) {
        admitted = std::unexpected(storage::Error{storage::ErrorCode::Missing, "Recovered magnet needs an explicit destination"});
      } else {
        admitted = engine->startMagnet(lease, intent, profile);
      }
      if (!admitted) { admissionFailures.insert_or_assign(hash, admitted.error()); }
      else { admissionFailures.erase(hash); }
    }
    readMagnets();
  }
  ui::Presentation query(std::stop_token stop) {
    loadVerifiedSeedIntents();
    checkpoint(stop);
    auto source = take(storage::readLibraryView(lease));
    readMagnets();
    checkpoint(stop);
    auto result = std::make_shared<ui::LibraryPresentation>();
    result->revision = revision = reserveLibraryRevision();
    std::map<std::string, int> counts;
    std::set<std::string> pauses;
    for (const auto& pause : source.pauses) { pauses.insert(pause.hash.hex()); }
    for (const auto& item : source.items) { if (item.saved.hash) { ++counts[item.saved.hash->hex()]; } }
    std::map<std::string, engine::ManagedDownloadStatus> live;
    if (engine) { for (auto& item : engine->managedDownloads()) { live.emplace(item.hash.hex(), std::move(item)); } }
    std::map<std::string, engine::PendingMagnetStatus> magnets;
    if (engine) {
      for (auto& item : engine->pendingMagnets()) {
        // Imported metadata may already be an ordinary row even if its optional
        // autostart failed. Report that admission once; retry remains ordinary.
        if (item.phase == engine::MagnetPhase::Imported && item.failure
            && (!reportedMagnetAdmissions.contains(item.hash.hex())
                || reportedMagnetAdmissions.at(item.hash.hex()) != item.admission)) {
          admissionFailures.insert_or_assign(item.hash.hex(), *item.failure);
          reportedMagnetAdmissions.insert_or_assign(item.hash.hex(), item.admission);
        }
        magnets.emplace(item.hash.hex(), std::move(item));
      }
    }
    movePools.clear();
    std::array<std::set<std::int64_t>, 2> poolPositions;
    bool classified = true;
    for (const auto& item : source.items) {
      if (!item.saved.hash || !item.saved.position || counts[item.saved.hash->hex()] != 1) { classified = false; break; }
      const auto found = live.find(item.saved.hash->hex());
      if (found == live.end() || !found->second.checked || found->second.failure) { classified = false; break; }
      const bool complete = found->second.selectedComplete;
      if (!poolPositions[complete ? 1 : 0].insert(*item.saved.position).second) { classified = false; break; }
      movePools.push_back({*item.saved.hash, complete});
    }
    for (const auto& pool : poolPositions) {
      if (!pool.empty() && (*pool.begin() != 1 || *pool.rbegin() != static_cast<std::int64_t>(pool.size()))) { classified = false; }
    }
    if (!classified) { movePools.clear(); }
    currentHashes.clear();
    pendingTargets.clear();
    result->items.reserve(source.items.size() + pendingIntents.size() + magnets.size());
    for (const auto& row : source.items) {
      const auto& saved = row.saved;
      const auto hash = saved.hash ? saved.hash->hex() : std::string{};
      QString name = text(saved.saveFile.value_or(std::filesystem::path(saved.saveDirectory).filename().string()));
      std::optional<qint64> size;
      std::optional<ui::SavedSelectedValues> savedSelected;
      if (row.cached) {
        const auto& cached = *row.cached;
        const auto selected = core::projectSavedDndProgress(cached.bytes, saved.completedBytes, cached.savedDnd);
        savedSelected = ui::SavedSelectedValues{.bytes = selected.selectedBytes, .remaining = selected.selectedRemaining,
            .percentThousandths = selected.percentThousandths, .defaults = selected.defaults, .diagnostics = selected.diagnostics};
        if (cached.displayName && !cached.displayName->empty()) { name = text(*cached.displayName); }
        else if (cached.utf8Name && !cached.utf8Name->empty()) { name = text(*cached.utf8Name); }
        else if (name.isEmpty() && cached.name) { name = text(*cached.name); }
        size = cached.bytes;
        known.insert_or_assign(hash, Known{name, size});
      } else if (const auto found = known.find(hash); found != known.end()) {
        name = found->second.name;
        size = found->second.bytes;
      }
      const bool paused = pauses.contains(hash);
      const bool stopped = saved.startup == core::StartupState::Stopped;
      QString state = paused ? (schedulerPaused.contains(hash)?SessionBridge::tr("Paused — Speed Scheduler"):SessionBridge::tr("Paused")) : stopped ? SessionBridge::tr("Stopped")
          : size && saved.completedBytes && *saved.completedBytes >= *size ? SessionBridge::tr("Complete")
          : saved.startup == core::StartupState::Queued ? SessionBridge::tr("Queued") : SessionBridge::tr("Waiting");
      const bool unique = !hash.empty() && counts[hash] == 1;
      if (unique) { currentHashes.insert(hash); }
      const auto key = unique ? hash : hash + ":" + std::to_string(saved.sourceIndex);
      quint16 actions = 0;
      const auto active = live.find(hash);
      const bool retained = active != live.end() && active->second.retained;
      if (active != live.end() && active->second.totalPieces > 0) {
        size = active->second.bytes; // Actual admitted metadata survives handle retirement.
        known.insert_or_assign(hash, Known{name, size});
      }
      const auto metadata = magnets.find(hash);
      const bool magnetRetained = metadata != magnets.end() && metadata->second.retained;
      const bool retiring = magnetRetained || (retained && (active->second.phase == engine::ManagedPhase::Stopping || active->second.failure));
      if (retained) {
        state = managedState(active->second.phase);
        if(active->second.phase==engine::ManagedPhase::Paused&&schedulerPaused.contains(hash)){state=tr("Paused — Speed Scheduler");}
        const auto reason = active->second.queueReason;
        if (active->second.phase == engine::ManagedPhase::Queued && reason) {
          switch (*reason) {
            case core::QueueReason::DownloadLimit: state = tr("Queued — download limit"); break;
            case core::QueueReason::ActiveLimit: state = tr("Queued — active torrent limit"); break;
            case core::QueueReason::StalledLimit: state = tr("Queued — stalled seed limit"); break;
            case core::QueueReason::IgnoreShareRatio: state = tr("Queued — share ratio reached"); break;
            case core::QueueReason::IgnoreZeroPeers: state = tr("Queued — no peers"); break;
            case core::QueueReason::IgnoreSeedCount: state = tr("Queued — seed count reached"); break;
            case core::QueueReason::IgnorePeerSeedRatio: state = tr("Queued — peer/seed ratio"); break;
            case core::QueueReason::WaitingForScrape: state = tr("Queued — waiting for tracker counts"); break;
            case core::QueueReason::TimedRotation: state = tr("Queued — timed rotation"); break;
            case core::QueueReason::UploadLimit: state = tr("Queued — upload limit"); break;
            case core::QueueReason::UnknownFacts: state = tr("Queued — rule observations unavailable"); break;
            default: break;
          }
        } else if (active->second.phase == engine::ManagedPhase::Seeding && active->second.firstPriority) {
          state = tr("Seeding — first priority");
        }
      }
      if (admissionFailures.contains(hash) || (active != live.end() && active->second.failure)) { state = SessionBridge::tr("Error"); }
      if (engine && engine->managedAvailable() && unique && saved.persistent && !retiring) {
        if (!retained || paused) { actions |= actionBit(core::DownloadAction::Start); }
        if (!stopped || retained || paused) { actions |= actionBit(core::DownloadAction::Stop); }
        if (!paused && !stopped) { actions |= actionBit(core::DownloadAction::Pause); }
        if (paused) { actions |= actionBit(core::DownloadAction::Resume); }
        if (!saved.forceStart) { actions |= actionBit(core::DownloadAction::ForceStart); }
        actions |= actionBit(core::DownloadAction::Queue);
        if (!movePools.empty()) {
          actions |= actionBit(core::DownloadAction::MoveTop) | actionBit(core::DownloadAction::MoveUp)
              | actionBit(core::DownloadAction::MoveDown) | actionBit(core::DownloadAction::MoveBottom);
        }
      }
      auto completed = saved.completedBytes && *saved.completedBytes >= 0 ? saved.completedBytes : std::nullopt;
      if (retained) { completed = active->second.completed; size = active->second.bytes; }
      result->items.push_back({.key = text(key), .hash = text(hash), .name = name, .state = state,
          .bytes = size, .completed = completed,
          .received = saved.downloaded, .sent = saved.uploaded,
          .canContinue = peerPort != 0 && unique && saved.persistent && !stopped && !paused && !retained && !magnetRetained,
          .savedPosition = saved.position, .savedSelected = std::move(savedSelected), .actionMask = actions});
    }
    std::set<std::string> pendingHashes;
    for (const auto& [hash, item] : pendingIntents) { (void)item; pendingHashes.insert(hash); }
    // An unresolved retained handle remains visible even if a later config read
    // fails or no longer contains its row. Never invent a cancellable CAS input.
    for (const auto& [hash, item] : magnets) { if (item.retained) { pendingHashes.insert(hash); } }
    for (const auto& hash : pendingHashes) {
      if (counts.contains(hash)) { continue; } // One ordinary row owns this identity after import.
      const auto saved = pendingIntents.find(hash);
      const auto active = magnets.find(hash);
      const auto* intent = saved == pendingIntents.end() ? nullptr : &saved->second;
      const auto* liveMagnet = active == magnets.end() ? nullptr : &active->second;
      QString state = SessionBridge::tr("Magnet needs attention");
      if (!pendingSourceCurrent) { state = SessionBridge::tr("Magnet read error"); }
      else if (liveMagnet && liveMagnet->phase == engine::MagnetPhase::Resolving) { state = SessionBridge::tr("Resolving metadata"); }
      else if (liveMagnet && liveMagnet->phase == engine::MagnetPhase::Retiring) { state = SessionBridge::tr("Retiring metadata"); }
      if ((liveMagnet && liveMagnet->failure) || admissionFailures.contains(hash)) { state = SessionBridge::tr("Magnet error"); }
      if (intent && !intent->destination) { state = SessionBridge::tr("Magnet needs destination"); }
      const auto name = intent && !intent->displayName.empty() ? intent->displayName
          : liveMagnet && !liveMagnet->name.empty() ? liveMagnet->name : hash;
      currentHashes.insert(hash);
      pendingTargets.insert(hash);
      result->items.push_back({.key = text(hash), .hash = text(hash), .name = text(name), .state = state,
          .actionMask = magnetActions(intent, liveMagnet)});
    }
    lastSnapshot = result;
    return result;
  }
};

SessionBridge::SessionBridge(QObject* parent, CountryProviderFactory factory, AzureusServiceFactory azureusFactory, SchedulerFactory schedulerFactory)
    : QObject(parent), countryProviderFactory_(std::move(factory)), azureusServiceFactory_(std::move(azureusFactory)), schedulerFactory_(std::move(schedulerFactory)) {
  qRegisterMetaType<ui::LocalRatingRequest>();
  qRegisterMetaType<ui::LocalRatingSubmission>();
  qRegisterMetaType<ui::LocalRatingPresentation>();
  qRegisterMetaType<ui::LocalRatingSavePresentation>();
  qRegisterMetaType<ui::SchedulerRequest>();
  qRegisterMetaType<ui::SchedulerSubmission>();
  qRegisterMetaType<ui::SchedulerPresentation>();
  qRegisterMetaType<ui::SchedulerSavePresentation>();
  qRegisterMetaType<ui::Target>();
  qRegisterMetaType<ui::DownloadTargets>();
  qRegisterMetaType<ui::FileIndices>();
  qRegisterMetaType<core::DownloadAction>();
  qRegisterMetaType<ui::Presentation>();
  qRegisterMetaType<ui::Telemetry>();
  qRegisterMetaType<ui::Details>();
  qRegisterMetaType<ui::TorrentIntakeRequest>();
  qRegisterMetaType<ui::TorrentIntakePresentation>();
  qRegisterMetaType<ui::TorrentIntakeSubmission>();
  qRegisterMetaType<ui::TorrentIntakeResultPresentation>();
  qRegisterMetaType<ui::SavedServicesPresentation>();
  qRegisterMetaType<ui::StatisticsPresentation>();
  qRegisterMetaType<ui::ManualTagStatisticsPresentation>();
  qRegisterMetaType<ui::CountryStatisticsPresentation>();
  qRegisterMetaType<core::HistoryDate>();
  qRegisterMetaType<ui::HistoryPresentation>();
  qRegisterMetaType<ui::HistoryMonthPresentation>();
  qRegisterMetaType<ui::HistoryRangePresentation>();
  qRegisterMetaType<core::HistoryRangeRequest>();
  qRegisterMetaType<ui::EngineStatisticsPresentation>();
  qRegisterMetaType<ui::NativeSessionPresentation>();
  qRegisterMetaType<ui::LiveSpeedManagerPresentation>();
  qRegisterMetaType<ui::OverallPresentation>();
  qRegisterMetaType<ui::RuntimeMetricsPresentation>();
  qRegisterMetaType<ui::AzureusStatisticsPresentation>();
  qRegisterMetaType<ui::ColumnLayoutsPresentation>();
  qRegisterMetaType<ui::ColumnLayoutSaveSubmission>();
  qRegisterMetaType<ui::ColumnLayoutSavePresentation>();
  completionMonitor_ = new QTimer(this);
  completionMonitor_->setInterval(20);
  connect(completionMonitor_, &QTimer::timeout, this, [this] {
    if (!current_ || !current_->result().valid()
        || current_->result().wait_for(std::chrono::seconds{0}) != std::future_status::ready) { return; }
    const auto result = current_->result().get();
    completionMonitor_->stop();
    // Executor can cancel a queued operation before its wrapper runs. Such a
    // future must also release the GUI's busy state and deferred window close.
    if (!result && !completionPosted_->load()) {
      QPointer<SessionBridge> guard(this);
      if (closingSession_) {
        closingSession_ = false;
        idleMonitor_->start();
        emit sessionCloseFailed();
        if (!guard) { return; }
      }
      busy_ = false;
      emit busyChanged(false);
      if (!guard) { return; }
      emit messageReady(result.error() == core::WorkError::Cancelled ? tr("Cancelled") : tr("Session command failed"));
    }
  });
  if (!worker_.start([this]() noexcept {
        if (host_ && host_->engine) { host_->engine->clearObservationSink(); }
        host_.reset(); // Worker-affine fallback abort/join, then held lease.
      })) { throw std::runtime_error("Cannot start native session worker"); }
  idleMonitor_ = new QTimer(this);
  idleMonitor_->setInterval(1000);
  connect(idleMonitor_, &QTimer::timeout, this, [this] {
    if (busy_ || closingSession_) { return; }
    if (idlePoll_) {
      if (idlePoll_->result().wait_for(std::chrono::seconds{0}) != std::future_status::ready) { return; }
      static_cast<void>(idlePoll_->result().get());
      idlePoll_.reset();
    }
    auto poll = worker_.submit([this](std::stop_token stop) -> core::WorkResult {
      if (!stop.stop_requested() && host_) {
        publishAzureus(); // Consume older RTT receipts before advancing source rate buckets.
        if (host_->engine) {
          static_cast<void>(host_->engine->poll()); tickScheduler(); tickAccounting();
          auto current = host_->engine->managedDownloads();
          bool changed = current.size() != host_->lastManagedPhases.size();
          for (const auto& item : current) {
            const Host::ManagedStamp stamp{item.phase,item.queueReason,
                item.firstPriority,item.unavailableQueueInputs,item.forceStart};
            auto [prior, added] = host_->lastManagedPhases.emplace(item.hash.hex(), stamp);
            changed = changed || added || prior->second != stamp;
            prior->second = stamp;
          }
          std::map<std::string, Host::MagnetStamp> magnets;
          for (const auto& item : host_->engine->pendingMagnets()) {
            magnets.emplace(item.hash.hex(), Host::MagnetStamp{item.admission, item.phase, item.retained,
                item.name, item.failure ? std::optional{item.failure->code} : std::nullopt,
                item.failure ? item.failure->message : std::string{}});
          }
          changed = changed || magnets != host_->lastMagnetPhases;
          host_->lastMagnetPhases = std::move(magnets);
          if (changed) {
            const auto snapshot = host_->query(stop);
            const auto session = host_->columnLayoutSession;
            publishManualTags();
            QMetaObject::invokeMethod(this, [this, snapshot, session] {
              if (!profileClosed_ && session == publishedColumnLayoutSession_) { emit snapshotReady(snapshot); }
            }, Qt::QueuedConnection);
          }
          else {
            for (const auto& item : current) {
              if (host_->currentHashes.contains(item.hash.hex()) && item.phase != engine::ManagedPhase::Stopped) {
                progress({{text(item.hash.hex()), host_->revision}, item.completed,
                    item.downloadRate, item.uploadRate, managedState(item.phase)});
              }
            }
          }
        }
      }
      return {};
    });
    if (poll) { idlePoll_.emplace(std::move(*poll)); }
  });
  idleMonitor_->start();
}
SessionBridge::~SessionBridge() {
  idleMonitor_->stop();
  if (current_) { current_->cancel(); }
  worker_.requestShutdown();
}
namespace { QString optionDefaultPath(const storage::ApplicationSettingsSnapshot&); engine::LiveQueueLimits optionQueues(const core::Document&); }

void SessionBridge::ready(ui::Presentation snapshot, QString message) {
  if (host_ && !host_->intakeDefaultLoaded) {
    host_->intakeDefaultLoaded = true;
    auto settings = storage::readApplicationSettings(host_->lease);
    if (settings) { host_->intakeDefault = optionDefaultPath(*settings); }
  }
  if (host_) {
    const auto notice = host_->pendingNotice();
    if (!notice.isEmpty()) { message = message.isEmpty() ? notice : message + QLatin1Char('\n') + notice; }
  }
  publishManualTags(); // Rejoin cached tags after an actual library revision change; no IO.
  // host_ is worker-owned. Publish its immutable identity by value on the GUI.
  const auto session = host_ ? host_->columnLayoutSession : quint64{0};
  QMetaObject::invokeMethod(this, [this, snapshot = std::move(snapshot), message = std::move(message), session, directory = host_ ? host_->intakeDefault : QString{}] {
    QPointer<SessionBridge> guard(this);
    if (session != 0 && publishedColumnLayoutSession_ == 0) {
      publishedColumnLayoutSession_ = session;
      profileClosed_ = false;
      // The initial optional read is queued by the UI; direct reentry remains busy.
      emit columnLayoutSessionReady(session);
      if (!guard) { return; }
    }
    completionMonitor_->stop();
    busy_ = false;
    emit busyChanged(false);
    if (!guard) { return; }
    emit snapshotReady(snapshot);
    if (!guard) { return; }
    emit intakeDefaultsReady(session, directory);
    if (!guard) { return; }
    if (!message.isEmpty()) { emit messageReady(message); }
  }, Qt::QueuedConnection);
}
void SessionBridge::failed(QString message) {
  QMetaObject::invokeMethod(this, [this, message = std::move(message)] {
    QPointer<SessionBridge> guard(this);
    completionMonitor_->stop();
    busy_ = false;
    emit busyChanged(false);
    if (!guard) { return; }
    emit messageReady(message);
  }, Qt::QueuedConnection);
}
bool SessionBridge::dispatch(std::move_only_function<void(std::stop_token)> operation, bool recoverLibrarySnapshot, bool closingOperation) {
  if (busy_ || (closingSession_ && !closingOperation)) { return false; }
  busy_ = true;
  auto posted = std::make_shared<std::atomic_bool>(false);
  auto submitted = worker_.submit([this, posted, operation = std::move(operation), recoverLibrarySnapshot, closingOperation](std::stop_token stop) mutable -> core::WorkResult {
    try { checkpoint(stop); operation(stop); posted->store(true); return {}; }
    catch (const Failure& failure) {
      const bool cancelled = failure.error.code == storage::ErrorCode::Cancelled || stop.stop_requested();
      const auto message = cancelled ? tr("Cancelled") : text(failure.error.message);
      if (closingOperation) { closeFailed(message); }
      else if (!recoverLibrarySnapshot) {
        // Optional configuration reads must not refresh or revise the library.
        failed(message);
      } else if (cancelled && host_ && host_->lastSnapshot) {
        auto snapshot = std::make_shared<ui::LibraryPresentation>(*host_->lastSnapshot);
        snapshot->revision = host_->revision = reserveLibraryRevision();
        host_->lastSnapshot = snapshot;
        ready(snapshot, message);
      } else if (host_ && !cancelled) {
        try { ready(host_->query({}), message); }
        catch (...) { failed(message); }
      } else { failed(message); }
      posted->store(true);
      return std::unexpected(cancelled ? core::WorkError::Cancelled : core::WorkError::Failed);
    } catch (const std::exception& failure) {
      if (closingOperation) { closeFailed(text(failure.what())); }
      else { failed(text(failure.what())); }
      posted->store(true);
      return std::unexpected(core::WorkError::Failed);
    }
  });
  if (!submitted) {
    QPointer<SessionBridge> guard(this);
    busy_ = false;
    emit busyChanged(false);
    if (!guard) { return false; }
    emit messageReady(tr("The session command queue is unavailable"));
    return false;
  }
  current_.emplace(std::move(*submitted));
  completionPosted_ = std::move(posted);
  completionMonitor_->start();
  emit busyChanged(true);
  return true;
}
namespace {
bool recoverOrReviewReplacement(const storage::ProfileLease& lease){
  const auto recovered=storage::recoverFileOperation(lease);if(recovered){return false;}
  const auto review=storage::prepareInterruptedReplacementReview(lease);
  if(!review){throw Failure{recovered.error()};}return true;
}
}
bool SessionBridge::open(std::filesystem::path profile, unsigned short ownedPeerPort) {
  return dispatch([this, profile = std::move(profile), ownedPeerPort](std::stop_token stop) {
    if (host_) { throw Failure{{storage::ErrorCode::Conflict, "A profile is already open"}}; }
    auto lease = take(storage::ProfileLease::acquire(profile));
    static_cast<void>(take(storage::recoverOriginalCompaction(lease)));
    const bool reviewOnly=recoverOrReviewReplacement(lease);
    if(!reviewOnly){static_cast<void>(take(storage::recoverTorrentIntake(lease)));}
    const auto session = reserveColumnLayoutSession();
    host_ = std::make_unique<Host>(profile, ownedPeerPort, std::move(lease), session);
    host_->fileRecoveryRequired=reviewOnly;host_->replacementReviewOnly=reviewOnly;
    if(reviewOnly){ready(host_->query(stop),tr("Profile opened read-only. Select the torrent and review its interrupted file replacement. The uncertain directory will be kept."));return;}
    const auto snapshot = host_->query(stop); // Validate the profile before optional engine startup.
    host_->azureusEnabled = static_cast<bool>(azureusServiceFactory_);
    host_->dhtAccounting.emplace(host_->azureusEnabled);
    if (azureusServiceFactory_) {
      try { host_->azureus = azureusServiceFactory_(host_->lease); }
      catch (...) {} // An optional service cannot invalidate an otherwise valid profile.
    }
    QString startupFailure;
    try { initializePersistentOwner(stop); }
    catch (const Failure& failure) {
      if (failure.error.code == storage::ErrorCode::Cancelled) { throw; }
      startupFailure = tr("Live statistics unavailable: %1").arg(text(failure.error.message));
    }
    ready(host_->engine ? host_->query(stop) : snapshot, std::move(startupFailure));
    publishAzureus(); // Posted after the profile identity; independent of the torrent engine.
  });
}
bool SessionBridge::openNormal(std::filesystem::path profile) {
  return dispatch([this, profile = std::move(profile)](std::stop_token stop) {
    if (host_) { throw Failure{{storage::ErrorCode::Conflict, "A profile is already open"}}; }
    auto lease = take(storage::ProfileLease::acquire(profile));
    host_ = std::make_unique<Host>(profile, 0, std::move(lease), reserveColumnLayoutSession());
    host_->normalNetwork = true;
    static_cast<void>(take(storage::recoverOriginalCompaction(host_->lease)));
    host_->replacementReviewOnly=recoverOrReviewReplacement(host_->lease);host_->fileRecoveryRequired=host_->replacementReviewOnly;
    if(host_->replacementReviewOnly){ready(host_->query(stop),tr("Profile opened read-only. Select the torrent and review its interrupted file replacement. The uncertain directory will be kept."));return;}
    static_cast<void>(take(storage::recoverTorrentIntake(host_->lease)));
    auto inventory = storage::ConfigFile::load(host_->lease, "downloads.config");
    if (!inventory && inventory.error().code == storage::ErrorCode::Missing) {
      static_cast<void>(take(storage::ConfigFile::createMissing(host_->lease, "downloads.config", "d9:downloadslee")));
    } else if (!inventory) { throw Failure{inventory.error()}; }
    static_cast<void>(host_->query(stop));
    host_->dhtAccounting.emplace(false);
    initializePersistentOwner(stop);
    if (!host_->engine) { throw Failure{{storage::ErrorCode::Unsupported, "This build lacks a qualified engine runtime"}}; }
    const auto source = take(storage::readLibraryView(host_->lease));
    std::set<std::string> paused;
    for (const auto& pause : source.pauses) { paused.insert(pause.hash.hex()); }
    for (const auto& item : source.items) {
      checkpoint(stop);
      const auto& saved = item.saved;
      if (!saved.hash || !saved.persistent || saved.startup == core::StartupState::Stopped
          || paused.contains(saved.hash->hex()) || !host_->currentHashes.contains(saved.hash->hex())) { continue; }
      auto started = host_->engine->startManaged(host_->lease,
          {*saved.hash, 0, saved.forceStart, saved.startup == core::StartupState::Queued});
      if (!started) { host_->admissionFailures.insert_or_assign(saved.hash->hex(), started.error()); }
    }
    host_->bootstrapMagnets(stop); // Ordinary admissions precede pending metadata recovery.
    tickScheduler();
    ready(host_->query(stop), host_->admissionFailures.empty() ? QString{} : tr("Some torrents require attention; saved data was preserved"));
  });
}
bool SessionBridge::downloadAction(ui::DownloadTargets targets, core::DownloadAction action) {
  if (profileClosed_ || targets.empty()) { return false; }
  return dispatch([this, targets = std::move(targets), action](std::stop_token stop) {
    if (!host_ || !host_->engine || !host_->lastSnapshot) {
      throw Failure{{storage::ErrorCode::Unsupported, "No managed engine is available"}};
    }
    if (host_->fileRecoveryRequired || storage::hasPendingOriginalCompaction(host_->lease)) { throw Failure{{storage::ErrorCode::Conflict, "Restart to recover the interrupted file operation before changing the profile"}}; }
    if (take(storage::recoverTorrentIntake(host_->lease))) {
      throw Failure{{storage::ErrorCode::Conflict, "Recovered a pending torrent import; library refreshed"}};
    }
    // Validate the entire UI selection before any saved intent or engine effect.
    std::set<std::string> unique;
    std::vector<core::TorrentHash> hashes;
    std::map<std::string, engine::PendingMagnet> pendingSelection;
    const bool hasPending = std::ranges::any_of(targets, [&](const auto& target) {
      return host_->pendingTargets.contains(target.hash.toStdString());
    });
    std::map<std::string, engine::PendingMagnet> freshPending;
    std::vector<engine::PendingMagnetStatus> metadata;
    if (hasPending) {
      // Whole-selection CAS admission before either pending or ordinary effects.
      for (auto& item : take(engine::readPendingMagnets(host_->lease, host_->networkPolicy))) {
        freshPending.emplace(item.hash.hex(), std::move(item));
      }
      metadata = host_->engine->pendingMagnets();
    }
    for (const auto& target : targets) {
      checkpoint(stop); host_->accepts(target);
      auto hash = identity(target);
      if (!unique.insert(hash.hex()).second) { throw Failure{{storage::ErrorCode::Conflict, "Duplicate download selection"}}; }
      const auto row = std::ranges::find_if(host_->lastSnapshot->items, [&](const auto& value) { return value.hash == target.hash; });
      if (row == host_->lastSnapshot->items.end() || !(row->actionMask & actionBit(action))) {
        throw Failure{{storage::ErrorCode::Conflict, "Download action is no longer available; refresh the library"}};
      }
      if (host_->pendingTargets.contains(hash.hex())) {
        const auto before = host_->pendingIntents.find(hash.hex());
        const auto fresh = freshPending.find(hash.hex());
        const auto live = std::ranges::find_if(metadata, [&](const auto& item) { return item.hash == hash; });
        if (before == host_->pendingIntents.end() || fresh == freshPending.end()
            || before->second.sourceEntry != fresh->second.sourceEntry
            || !(host_->magnetActions(&fresh->second, live == metadata.end() ? nullptr : &*live) & actionBit(action))) {
          throw Failure{{storage::ErrorCode::Conflict, "Pending magnet changed; refresh the library"}};
        }
        pendingSelection.emplace(hash.hex(), fresh->second);
      }
      hashes.push_back(std::move(hash));
    }
    QStringList failures;
    // Withdraw EVERY accepted selected ticket before the first target's IO.
    // A failure for A must not leave selected B authorized to auto-publish.
    std::set<std::string> cancelledVerification;
    const auto admissions=host_->engine->managedDownloads();
    for(const auto& hash:hashes){if(std::ranges::any_of(admissions,[&](const auto& item){return item.hash==hash&&item.verificationHeld;})){
      cancelledVerification.insert(hash.hex());
    }}
    for(const auto& hash:hashes){if(cancelledVerification.contains(hash.hex())){
      const auto cancelled=host_->engine->stopManaged(hash);
      if(!cancelled){failures.push_back(text(cancelled.error().message));}
    }}
    host_->verifiedSeedPending.insert(cancelledVerification.begin(),cancelledVerification.end());
    for (const auto& hash : hashes) {
      checkpoint(stop);
      if (const auto pending = pendingSelection.find(hash.hex()); pending != pendingSelection.end()) {
        std::expected<void, storage::Error> controlled;
        if (action == core::DownloadAction::Start) {
          controlled = host_->engine->startMagnet(host_->lease, pending->second, host_->profile);
        } else {
          const auto current = host_->engine->pendingMagnets();
          const auto live = std::ranges::find_if(current, [&](const auto& item) { return item.hash == hash; });
          // The engine owns retirement whenever an exact handle is retained.
          // Adapter-only cancellation is safe solely without a metadata handle.
          controlled = live != current.end() && live->retained ? host_->engine->cancelMagnet(hash)
              : engine::cancelPendingMagnet(host_->lease, pending->second);
        }
        if (!controlled) {
          host_->admissionFailures.insert_or_assign(hash.hex(), controlled.error());
          failures.push_back(text(hash.hex()) + QStringLiteral(": ") + text(controlled.error().message));
        } else { host_->admissionFailures.erase(hash.hex()); }
        continue;
      }
      auto beforeControl=host_->engine->managedDownloads();
      const bool wasChecking=cancelledVerification.contains(hash.hex());
      const bool manual=static_cast<unsigned>(action)<=static_cast<unsigned>(core::DownloadAction::ForceStart)||wasChecking;
      if(manual){
        auto& revision=host_->manualRevisions[hash.hex()];
        if(revision==std::numeric_limits<std::uint64_t>::max()){throw Failure{{storage::ErrorCode::Conflict,"Manual action revision exhausted"}};}
        ++revision;host_->schedulerPaused.erase(hash.hex());
        if(host_->scheduler){
          const auto revoked=host_->scheduler->revokeForManual(host_->lease,hash);
          if(!revoked){failures.push_back(tr("Scheduler ownership was revoked; cleanup needs attention: %1").arg(text(revoked.error().message)));}
        }
      }
      const auto seedIntent=take(storage::readVerifiedSeedIntent(host_->lease,hash));
      const bool releasedActive=std::ranges::any_of(beforeControl,[&](const auto& item){return item.hash==hash&&item.retained
          &&item.verifiedSeeding==engine::VerifiedSeedingPhase::Released&&!item.failure;});
      if(seedIntent&&!releasedActive&&manual&&(action==core::DownloadAction::Start||action==core::DownloadAction::Resume
          ||action==core::DownloadAction::Queue||action==core::DownloadAction::ForceStart)){
        host_->verifiedSeedPending.insert(hash.hex());
        if(wasChecking||action==core::DownloadAction::Queue||action==core::DownloadAction::ForceStart){
          failures.push_back(tr("Verification was kept stopped. Use Start after retirement to verify existing data again."));continue;
        }
        const auto live=std::ranges::find_if(beforeControl,[&](const auto& item){return item.hash==hash&&item.retained;});
        if(live==beforeControl.end()){
          const auto reset=storage::resetVerifiedSeedForRetry(host_->lease,hash);
          if(!reset){failures.push_back(text(reset.error().message));continue;}
          const auto retry=host_->engine->startManaged(host_->lease,{.hash=hash,.verifyExisting=true});
          if(!retry){host_->admissionFailures.insert_or_assign(hash.hex(),retry.error());failures.push_back(text(retry.error().message));}
          continue;
        }
      }
      auto changed = storage::applyLibraryCommand(host_->lease, hash, action, host_->movePools);
      if (!changed) { failures.push_back(text(hash.hex()) + QStringLiteral(": ") + text(changed.error().message)); continue; }
      host_->admissionFailures.erase(hash.hex());
      std::expected<void, storage::Error> controlled;
      auto states = host_->engine->managedDownloads();
      const auto live = std::ranges::find_if(states, [&](const auto& value) { return value.hash == hash; });
      const bool active = live != states.end() && live->retained;
      switch (action) {
        case core::DownloadAction::Stop: controlled = host_->engine->stopManaged(hash); break;
        case core::DownloadAction::Pause:
          if (active) { controlled = host_->engine->pauseManaged(hash, true); } break;
        case core::DownloadAction::Start: case core::DownloadAction::Resume:
        case core::DownloadAction::Queue: case core::DownloadAction::ForceStart:
          if (changed->pauseAfter || changed->after.startup == core::StartupState::Stopped) { break; }
          if (active) { controlled = host_->engine->queueManaged(hash, changed->after.forceStart,
              changed->after.startup == core::StartupState::Queued); }
          else { controlled = host_->engine->startManaged(host_->lease,
              {hash, host_->peerPort, changed->after.forceStart, changed->after.startup == core::StartupState::Queued}); }
          break;
        default: {
          auto source = take(storage::readLibraryView(host_->lease));
          std::ranges::stable_sort(source.items, {}, [](const auto& item) { return item.saved.position.value_or(0); });
          std::vector<core::TorrentHash> order;
          for (const auto& item : source.items) { if (item.saved.hash) { order.push_back(*item.saved.hash); } }
          controlled = host_->engine->reorderManaged(order);
          break;
        }
      }
      if (!controlled) {
        host_->admissionFailures.insert_or_assign(hash.hex(), controlled.error());
        failures.push_back(text(hash.hex()) + QStringLiteral(": ") + text(controlled.error().message));
      }
    }
    ready(host_->query({}), failures.empty() ? tr("Download command applied") : failures.join(QLatin1Char('\n')));
  });
}
bool SessionBridge::torrentImport(QString metadata, QString destination, bool start) {
  if (profileClosed_ || metadata.isEmpty() || destination.isEmpty()) { return false; }
  return dispatch([this, metadata = std::move(metadata), destination = std::move(destination), start](std::stop_token stop) {
    if (!host_ || !host_->engine || !host_->engine->managedAvailable()) {
      throw Failure{{storage::ErrorCode::Unsupported, "No managed engine is available"}};
    }
    checkpoint(stop);
    if (host_->fileRecoveryRequired || storage::hasPendingOriginalCompaction(host_->lease)) { throw Failure{{storage::ErrorCode::Conflict, "Restart to recover the interrupted file operation before changing the profile"}}; }
    static_cast<void>(take(storage::recoverTorrentIntake(host_->lease)));
    const auto hash = take(storage::importTorrent(host_->lease,
        std::filesystem::path(metadata.toUtf8().toStdString()),
        std::filesystem::path(destination.toUtf8().toStdString()), !start));
    const auto details = engine::queryTorrent(host_->lease, hash);
    if (details) { host_->known.insert_or_assign(hash.hex(), Host::Known{text(details->name), details->bytes}); }
    QString message = tr("Torrent added");
    if (start) {
      auto admitted = host_->engine->startManaged(host_->lease, {hash, host_->peerPort, false, false});
      if (!admitted) {
        host_->admissionFailures.insert_or_assign(hash.hex(), admitted.error());
        message = text(admitted.error().message);
      }
    }
    ready(host_->query({}), message);
  });
}
bool SessionBridge::previewTorrentIntake(ui::TorrentIntakeRequest request) {
  if(profileClosed_||!request.session||request.session!=publishedColumnLayoutSession_||!request.request){return false;}
  return dispatch([this,request=std::move(request)](std::stop_token stop){
    auto packet=std::make_shared<ui::TorrentIntakePacket>();packet->source=request;
    try {
      if(!host_||request.session!=host_->columnLayoutSession){throw Failure{{storage::ErrorCode::Conflict,"The profile changed; load the torrent again"}};}
      if(host_->intakeRequest&&request.request<=host_->intakeRequest->request){throw Failure{{storage::ErrorCode::Conflict,"This file preview request is stale"}};}
      host_->intakeRequest=request;host_->intakeMetadata.reset();
      const auto filenameContext=take(storage::readOriginalFilenameContext(host_->lease));
      auto value=take(storage::inspectTorrentIntake(std::filesystem::path(request.metadata.toUtf8().toStdString()),stop,filenameContext.conversions,filenameContext.decoderShowAll));
      if(take(storage::readOriginalFilenameContext(host_->lease))!=filenameContext){throw Failure{{storage::ErrorCode::Conflict,"Filename context changed during file preview; load it again"}};}
      if(value.layout().files.size()>ui::MaximumIntakePreviewFiles){throw Failure{{storage::ErrorCode::Unsupported,"This torrent has too many files for the preview editor"}};}
      if(host_->intakePreviewRevision==std::numeric_limits<quint64>::max()){throw Failure{{storage::ErrorCode::Conflict,"File preview revisions are exhausted"}};}
      packet->revision=++host_->intakePreviewRevision;packet->hash=text(value.hash().hex());packet->name=text(value.layout().name);packet->trackers=value.trackers();packet->initialDestinations=value.initialDestinations();packet->multifile=value.layout().multifile;packet->decoderShowAll=value.decoderShowAll();
      for(std::size_t n=0;n<value.layout().files.size();++n){
        const auto& file=value.layout().files[n];packet->files.push_back({static_cast<int>(n),text(file.path),file.bytes,file.padding});
      }
      if(!ui::validTorrentIntake(packet)){throw Failure{{storage::ErrorCode::Unsupported,"Torrent paths exceed the preview editor limits"}};}
      checkpoint(stop);host_->intakeMetadata.emplace(std::move(value));
    }catch(const Failure& error){packet->failure=text(error.error.message);}
    catch(const std::exception&){packet->failure=tr("The torrent file list could not be loaded");}
    ui::TorrentIntakePresentation result=std::move(packet);
    QMetaObject::invokeMethod(this,[this,result]{
      QPointer<SessionBridge> guard(this);
      if(!profileClosed_&&result->source.session==publishedColumnLayoutSession_){emit torrentIntakeReady(result);}
      if(!guard){return;}completionMonitor_->stop();busy_=false;emit busyChanged(false);
    },Qt::QueuedConnection);
  },false);
}
bool SessionBridge::importTorrentFiles(ui::TorrentIntakeSubmission submission) {
  if(profileClosed_||!submission.session||submission.session!=publishedColumnLayoutSession_||!submission.request){return false;}
  return dispatch([this,submission=std::move(submission)](std::stop_token stop){
    auto result=std::make_shared<ui::TorrentIntakeResultPacket>();
    result->session=submission.session;result->request=submission.request;result->revision=submission.revision;
    try {
      if(!host_||submission.session!=host_->columnLayoutSession||!host_->intakeRequest||!host_->intakeMetadata
          ||submission.request!=host_->intakeRequest->request||submission.revision!=host_->intakePreviewRevision){
        throw Failure{{storage::ErrorCode::Conflict,"The file preview is stale; load the torrent again"}};
      }
      if(host_->fileRecoveryRequired||storage::hasPendingFileOperation(host_->lease)
          ||storage::hasPendingOriginalCompaction(host_->lease)||storage::hasPendingTorrentIntake(host_->lease)){
        throw Failure{{storage::ErrorCode::Conflict,"Restart to finish pending storage recovery before importing"}};
      }
      if(!core::validTorrentIntakeLimits(submission.limits)){throw Failure{{storage::ErrorCode::InvalidFormat,"Per-torrent limits must be between zero and INT32_MAX bytes/s"}};}
      if(submission.trackers&&!core::validTorrentIntakeTrackerTiers(*submission.trackers)){throw Failure{{storage::ErrorCode::InvalidFormat,"Invalid tracker tier override"}};}
      std::optional<std::string> saveName;
      if(submission.saveName){saveName=submission.saveName->toUtf8().toStdString();if(QString::fromUtf8(saveName->data(),static_cast<qsizetype>(saveName->size()))!=*submission.saveName||!core::validTorrentIntakeSaveName(*saveName)){throw Failure{{storage::ErrorCode::UnsafePath,"Save name must be one safe UTF8 filename component"}};}}
      if(submission.decoderShowAll!=host_->intakeMetadata->decoderShowAll()){throw Failure{{storage::ErrorCode::Conflict,"The file preview decoder context is stale"}};}
      const auto& layout=host_->intakeMetadata->layout();
      if(submission.files.size()!=layout.files.size()){throw Failure{{storage::ErrorCode::Conflict,"File choices do not match the loaded torrent"}};}
      bool selected=false;
      for(std::size_t n=0;n<submission.files.size();++n){
        if(layout.files[n].padding&&(!submission.files[n].skipped||submission.files[n].priority)){
          throw Failure{{storage::ErrorCode::Unsupported,"Padding files cannot be selected"}};
        }
        selected=selected||(!layout.files[n].padding&&!submission.files[n].skipped&&layout.files[n].bytes>0);
      }
      if(submission.verifyExisting&&(submission.start||submission.forceStart||submission.paused||!selected)){
        throw Failure{{storage::ErrorCode::InvalidFormat,"Verified seeding starts stopped, without force or pause"}};
      }
      const bool paused=selected&&submission.paused;
      if(paused&&(submission.start||submission.forceStart)){throw Failure{{storage::ErrorCode::InvalidFormat,"Paused import conflicts with start/force intent"}};}
      const bool start=submission.start&&selected&&!paused;
      const bool force=start&&submission.forceStart;const bool queued=start&&!force;
      if((start||submission.verifyExisting)&&(!host_->engine||!host_->engine->managedAvailable())){throw Failure{{storage::ErrorCode::Unsupported,"The engine is unavailable; add this torrent stopped instead"}};}
      checkpoint(stop); // No payload/network work happens before the explicit commit.
      const auto hash=take(storage::importTorrentWithFileOptions(host_->lease,*host_->intakeMetadata,
          std::filesystem::path(submission.destination.toUtf8().toStdString()),!start,submission.files,nullptr,force,stop,submission.limits,submission.sequentialDownload,queued,paused,submission.trackers,std::move(saveName),submission.verifyExisting,std::move(submission.fileDestinations),submission.goal));
      result->imported=true;result->hash=text(hash.hex());host_->intakeMetadata.reset();
      const auto details=engine::queryTorrent(host_->lease,hash);
      if(details){host_->known.insert_or_assign(hash.hex(),Host::Known{text(details->displayName&&!details->displayName->empty()?*details->displayName:details->name),details->bytes});}
      if(start||submission.verifyExisting){
        if(submission.verifyExisting){host_->verifiedSeedPending.insert(hash.hex());}
        const auto admitted=host_->engine->startManaged(host_->lease,
            {.hash=hash,.ownedLoopbackPeerPort=submission.verifyExisting?std::uint16_t{0}:host_->peerPort,.forceStart=force,.queued=queued,.verifyExisting=submission.verifyExisting});
        if(admitted){result->started=!submission.verifyExisting;result->verificationPending=submission.verifyExisting;host_->admissionFailures.erase(hash.hex());}
        else{host_->admissionFailures.insert_or_assign(hash.hex(),admitted.error());result->failure=(submission.verifyExisting?tr("Torrent added stopped; existing-data verification could not start: %1"):tr("Torrent added; download could not start: %1")).arg(text(admitted.error().message));}
      }
    }catch(const Failure& error){result->failure=text(error.error.message);}
    catch(const std::exception&){result->failure=tr("Torrent import did not finish; inspect recovery status before retrying");}
    if(host_&&storage::hasPendingTorrentIntake(host_->lease)){host_->fileRecoveryRequired=true;result->failure+=tr(" Restart to recover the accepted import.");}
    ui::TorrentIntakeResultPresentation packet=result;
    QMetaObject::invokeMethod(this,[this,packet]{
      if(!profileClosed_&&packet->session==publishedColumnLayoutSession_){emit torrentIntakeFinished(packet);}
    },Qt::QueuedConnection);
    if(host_){ready(host_->query({}),result->failure.isEmpty()?(result->verificationPending?tr("Torrent added stopped; checking existing data before seeding"):tr("Torrent added with the selected file choices")):result->failure);}
    else{failed(result->failure);}
  });
}

bool SessionBridge::magnetImport(QString uri, QString destination) {
  if (profileClosed_ || uri.isEmpty() || destination.isEmpty()) { return false; }
  return dispatch([this, uri = std::move(uri), destination = std::move(destination)](std::stop_token stop) {
    if (!host_ || !host_->engine || !host_->engine->managedAvailable()) {
      throw Failure{{storage::ErrorCode::Unsupported, "No managed engine is available"}};
    }
    checkpoint(stop);
    if (host_->fileRecoveryRequired || storage::hasPendingOriginalCompaction(host_->lease)) { throw Failure{{storage::ErrorCode::Conflict, "Restart to recover the interrupted file operation before changing the profile"}}; }
    static_cast<void>(take(storage::recoverTorrentIntake(host_->lease)));
    const auto intent = take(engine::parseMagnetIntent(uri.toUtf8().toStdString(),
        std::filesystem::path(destination.toUtf8().toStdString()), false, host_->networkPolicy));
    const auto library = take(storage::readLibraryView(host_->lease));
    for (const auto& item : library.items) {
      if (item.saved.hash && *item.saved.hash == intent.hash) {
        throw Failure{{storage::ErrorCode::Conflict, "This magnet is already in the library"}};
      }
    }
    const auto existing = take(engine::readPendingMagnets(host_->lease, host_->networkPolicy));
    const auto prior = std::ranges::find_if(existing, [&](const auto& value) { return value.hash == intent.hash; });
    for (const auto& active : host_->engine->pendingMagnets()) {
      if (active.hash == intent.hash && active.retained) {
        throw Failure{{storage::ErrorCode::Conflict, "This magnet is already resolving or retiring"}};
      }
    }
    if (prior != existing.end() && (prior->uri != intent.uri || prior->requested != intent.requested)) {
      throw Failure{{storage::ErrorCode::Conflict, "A different magnet request already owns this identity"}};
    }
    checkpoint(stop); // From persistence onward, failures retain the original recovery row.
    const auto persisted = prior == existing.end()
        ? take(engine::queuePendingMagnet(host_->lease, intent, host_->networkPolicy))
        : take(engine::attachPendingMagnetDestination(host_->lease, *prior, *intent.destination, host_->networkPolicy));
    const auto admitted = host_->engine->startMagnet(host_->lease, persisted, host_->profile);
    if (!admitted) { host_->admissionFailures.insert_or_assign(persisted.hash.hex(), admitted.error()); }
    else { host_->admissionFailures.erase(persisted.hash.hex()); }
    ready(host_->query({}), admitted ? tr("Resolving magnet metadata") : text(admitted.error().message));
  });
}
bool SessionBridge::filePolicy(ui::Target target, ui::FileIndices selected, int priority, bool skipped) {
  if (profileClosed_ || selected.empty()) { return false; }
  return dispatch([this, target = std::move(target), selected = std::move(selected), priority, skipped](std::stop_token stop) {
    if (!host_ || !host_->engine || !host_->engine->managedAvailable()) {
      throw Failure{{storage::ErrorCode::Unsupported, "No managed engine is available"}};
    }
    if (host_->fileRecoveryRequired || storage::hasPendingOriginalCompaction(host_->lease)) { throw Failure{{storage::ErrorCode::Conflict, "Restart to recover the interrupted file operation before changing the profile"}}; }
    if (take(storage::recoverTorrentIntake(host_->lease))) {
      throw Failure{{storage::ErrorCode::Conflict, "Recovered a pending torrent import; library refreshed"}};
    }
    host_->accepts(target);
    const auto hash = identity(target);
    const auto files = take(engine::queryTorrent(host_->lease, hash));
    std::vector<compat::FilePolicy> policies;
    policies.reserve(files.entries.size());
    for (const auto& file : files.entries) { policies.push_back({file.padding || file.skipped, file.padding ? 0 : file.priority}); }
    std::set<int> unique;
    for (const int index : selected) {
      if (index < 0 || static_cast<std::size_t>(index) >= files.entries.size() || files.entries[static_cast<std::size_t>(index)].padding
          || !unique.insert(index).second || files.entries[static_cast<std::size_t>(index)].index != index) {
        throw Failure{{storage::ErrorCode::Conflict, "This file selection is stale or includes padding"}};
      }
      policies[static_cast<std::size_t>(index)] = {skipped, skipped ? 0 : priority};
    }
    // Validate every requested preference before retiring the exact admission.
    std::set<std::int32_t> ranks;
    for(const auto& policy:policies){
      if(policy.priority==std::numeric_limits<std::int32_t>::min()){
        throw Failure{{storage::ErrorCode::Unsupported,"Priority cannot round trip through original encoding"}};
      }
      if(!policy.skipped){ranks.insert(policy.priority);}
    }
    if(ranks.size()>7){throw Failure{{storage::ErrorCode::Unsupported,"At most seven distinct file priorities are supported"}};}
    bool readmit=false,paused=false,force=false,queued=false;std::uint64_t oldAdmission=0;
    {
      const auto active=host_->engine->managedDownloads();const auto found=std::ranges::find(active,hash,&engine::ManagedDownloadStatus::hash);
      if(found!=active.end()){
        if(found->failure){throw Failure{*found->failure};}
        if(found->phase==engine::ManagedPhase::Stopping){throw Failure{{storage::ErrorCode::Busy,"Torrent retirement is still pending"}};}
        readmit=found->retained;paused=found->manuallyPaused;force=found->forceStart;oldAdmission=found->admission;
      }
      const auto original=take(storage::readLibraryView(host_->lease));
      for(const auto& row:original.items){if(row.saved.hash&&*row.saved.hash==hash){force=row.saved.forceStart;queued=row.saved.startup==core::StartupState::Queued;}}
      for(const auto& pause:original.pauses){if(pause.hash==hash){paused=true;}}
    }
    if(readmit){
      take(host_->engine->stopManaged(hash));const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds{6};
      for(;;){
        checkpoint(stop);const auto active=host_->engine->managedDownloads();const auto found=std::ranges::find(active,hash,&engine::ManagedDownloadStatus::hash);
        if(found==active.end()){throw Failure{{storage::ErrorCode::Conflict,"File-selection admission disappeared during retirement"}};}
        if(found->failure){throw Failure{*found->failure};}
        if(!found->retained){take(host_->engine->forgetManaged(hash));break;}
        if(std::chrono::steady_clock::now()>=deadline){throw Failure{{storage::ErrorCode::Busy,"Torrent files are still retained; file selection was not changed"}};}
        if(!host_->engine->poll()){throw Failure{{storage::ErrorCode::Io,"Cannot certify file-selection retirement"}};}
        std::this_thread::sleep_for(std::chrono::milliseconds{20});
      }
    }
    checkpoint(stop);
    auto changed=storage::setFilePolicies(host_->lease,hash,policies);
    if(!changed){host_->fileRecoveryRequired=storage::hasPendingFileOperation(host_->lease)||storage::hasPendingOriginalCompaction(host_->lease);throw Failure{changed.error()};}
    host_->filePlan.reset();host_->filePlanPacket.reset();
    if(readmit){
      auto admitted=host_->engine->startManaged(host_->lease,{hash,host_->peerPort,force,queued,paused});
      if(!admitted){host_->admissionFailures.insert_or_assign(hash.hex(),admitted.error());throw Failure{{admitted.error().code,"File selection saved; torrent could not restart: "+admitted.error().message}};}
      if(paused&&oldAdmission&&host_->scheduler){
        const auto active=host_->engine->managedDownloads();const auto found=std::ranges::find(active,hash,&engine::ManagedDownloadStatus::hash);
        if(found==active.end()||!found->retained||!found->manuallyPaused){throw Failure{{storage::ErrorCode::Conflict,"File selection saved; scheduler pause could not be rebound"}};}
        take(host_->scheduler->rebindOwnedPause(host_->lease,hash,oldAdmission,found->admission,host_->manualRevisions[hash.hex()]));
      }
      host_->admissionFailures.erase(hash.hex());host_->lastManagedPhases.erase(hash.hex());
    }
    ready(host_->query({}), tr("File selection saved"));
  });
}
bool SessionBridge::compactFile(ui::Target target, int originalIndex) {
  if (profileClosed_ || originalIndex < 0) { return false; }
  return dispatch([this, target = std::move(target), originalIndex](std::stop_token stop) {
    if (!host_) { throw Failure{{storage::ErrorCode::Missing, "No profile is open"}}; }
    host_->accepts(target);
    if (host_->engine && !host_->engine->poll()) {
      throw Failure{{storage::ErrorCode::Io, "The engine cannot confirm safe idle storage; close the profile before reclaiming space"}};
    }
    if (!host_->compactionReady()) {
      throw Failure{{storage::ErrorCode::Busy, "Stop all torrents and metadata downloads, wait for retirement, then refresh file details before reclaiming space"}};
    }
    const auto hash = identity(target);
    const auto files = take(engine::queryTorrent(host_->lease, hash));
    if (static_cast<std::size_t>(originalIndex) >= files.entries.size()
        || files.entries[static_cast<std::size_t>(originalIndex)].index != originalIndex
        || files.entries[static_cast<std::size_t>(originalIndex)].padding) {
      throw Failure{{storage::ErrorCode::Conflict, "This file selection is stale or includes padding"}};
    }
    checkpoint(stop);
    // These records can still own authenticated ManagedStorage descriptors even
    // after LT weak-expiration. forgetManaged checks actual flush/removal proof.
    // Never stop/restart a torrent, revoke a scheduler pause or infer a seal here.
    if (host_->engine) {
      const auto retired = host_->engine->managedDownloads();
      for (const auto& item : retired) { take(host_->engine->forgetManaged(item.hash)); }
    }
    host_->accepts(target);
    if (!host_->compactionReady()) { throw Failure{{storage::ErrorCode::Conflict, "Torrent retirement is not available for space reclamation"}}; }
    auto result = storage::compactOriginalFile(host_->lease, hash, static_cast<std::size_t>(originalIndex), stop);
    host_->fileRecoveryRequired = storage::hasPendingFileOperation(host_->lease)
        || storage::hasPendingOriginalCompaction(host_->lease);
    host_->filePlan.reset(); host_->filePlanPacket.reset();
    if (!result) {
      const auto message = host_->fileRecoveryRequired
          ? tr("Space reclamation did not finish. Restart to recover it before using these files. %1").arg(text(result.error().message))
          : tr("Space reclamation did not finish. The file may remain set to Do not download. %1").arg(text(result.error().message));
      ready(host_->query({}), message); // Publish any accepted Skip, including cancellation after it.
      return;
    }
    ready(host_->query({}), tr("File set to Do not download; %1 bytes reclaimed. Boundary data was kept.")
        .arg(static_cast<qulonglong>(result->previousPhysicalBytes - result->physicalBytes)));
  });
}

bool SessionBridge::refresh() {
  if (profileClosed_) { return false; }
  return dispatch([this](std::stop_token stop) {
    if (!host_) { throw Failure{{storage::ErrorCode::Missing, "No profile is open"}}; }
    ready(host_->query(stop));
  });
}
bool SessionBridge::savedServices(quint64 request) {
  if (profileClosed_) { return false; }
  if (request == 0) { return false; }
  return dispatch([this, request](std::stop_token stop) {
    if (!host_) { throw Failure{{storage::ErrorCode::Missing, "No profile is open"}}; }
    auto packet = std::make_shared<ui::SavedServicesPacket>();
    packet->request = request;
    checkpoint(stop);
    packet->hosting = savedResult<core::SavedHosting>(storage::readSavedHostingView(host_->lease, stop));
    checkpoint(stop);
    packet->sharing = savedResult<core::SavedShares>(storage::readSavedSharesView(host_->lease, stop));
    checkpoint(stop);
    ui::SavedServicesPresentation presentation = std::move(packet);
    QMetaObject::invokeMethod(this, [this, presentation = std::move(presentation), stop] {
      QPointer<SessionBridge> guard(this);
      if (stop.stop_requested()) {
        completionMonitor_->stop();
        busy_ = false;
        emit busyChanged(false);
        if (guard) { emit messageReady(tr("Cancelled")); }
        return;
      }
      // Keep busy while delivering the packet: a direct result slot must not
      // replace current_ or dispatch another command during this completion.
      emit savedServicesReady(presentation);
      if (!guard) { return; }
      completionMonitor_->stop();
      busy_ = false;
      emit busyChanged(false);
    }, Qt::QueuedConnection);
  });
}
bool SessionBridge::statistics(quint64 session, quint64 request) {
  if (session == 0 || request == 0 || session != publishedColumnLayoutSession_) { return false; }
  return dispatch([this, session, request](std::stop_token stop) {
    if (!host_ || host_->columnLayoutSession != session) {
      throw Failure{{storage::ErrorCode::Conflict, "This statistics session is no longer active"}};
    }
    auto packet = std::make_shared<ui::StatisticsPacket>();
    packet->session = session;
    packet->request = request;
    checkpoint(stop);
    auto view = storage::readSavedStatisticsView(host_->lease, stop);
    checkpoint(stop);
    if (!view) {
      if (view.error().code == storage::ErrorCode::Cancelled) { throw Failure{std::move(view.error())}; }
      packet->failure = text(view.error().message);
    } else if (view->missing()) {
      packet->state = ui::StatisticsReadState::Missing;
    } else if (!view->recoverySource) {
      packet->failure = tr("Saved statistics source is unavailable");
    } else {
      switch (*view->recoverySource) {
        case storage::RecoverySource::Primary: packet->source = ui::StatisticsReadSource::Primary; break;
        case storage::RecoverySource::Backup: packet->source = ui::StatisticsReadSource::Backup; break;
        case storage::RecoverySource::Saving: packet->source = ui::StatisticsReadSource::Saving; break;
      }
      if (packet->source) {
        packet->snapshot = std::make_shared<const core::SavedStatistics>(std::move(*view->snapshot));
        packet->state = ui::StatisticsReadState::Ready;
      }
    }
    checkpoint(stop);
    // Saved capacity is independent of Overall: optional parse/read failures
    // never turn an otherwise valid saved-totals read into a failure.
    try {
      auto speed = std::make_shared<ui::SavedSpeedManagerPacket>();
      speed->session = session; speed->sequence = request;
      speed->livePolicy = ui::AutoSpeedPolicy::Inactive;
      auto settings = storage::ConfigFile::load(host_->lease, "biglybt.config");
      if (!settings && settings.error().code != storage::ErrorCode::Missing) {
        speed->failure = text(settings.error().message);
      } else {
        auto saved = storage::readSavedSpeedManagerView(host_->lease,
            settings ? std::string_view(settings->document().originalBytes()) : std::string_view("de"), stop);
        if (!saved) { speed->failure = text(saved.error().message); }
        else {
          speed->settings = std::make_shared<const core::SavedSpeedSettings>(std::move(saved->settings));
          if (saved->history && saved->recoverySource) {
            speed->history = std::make_shared<const core::SavedSpeedHistory>(std::move(*saved->history));
            switch (*saved->recoverySource) {
              case storage::RecoverySource::Primary: speed->source = ui::SavedSpeedManagerSource::Primary; break;
              case storage::RecoverySource::Backup: speed->source = ui::SavedSpeedManagerSource::Backup; break;
              case storage::RecoverySource::Saving: speed->source = ui::SavedSpeedManagerSource::Saving; break;
            }
            speed->state = ui::SavedSpeedManagerState::Ready;
          } else { speed->state = ui::SavedSpeedManagerState::Missing; }
        }
      }
      packet->savedSpeed = std::move(speed);
    } catch (...) {} // Optional presentation allocation is outside saved totals.
    checkpoint(stop);
    ui::StatisticsPresentation presentation = std::move(packet);
    QMetaObject::invokeMethod(this, [this, presentation = std::move(presentation), stop] {
      QPointer<SessionBridge> guard(this);
      if (stop.stop_requested() || presentation->session != publishedColumnLayoutSession_) {
        completionMonitor_->stop();
        busy_ = false;
        emit busyChanged(false);
        if (guard && stop.stop_requested()) { emit messageReady(tr("Cancelled")); }
        return;
      }
      // Keep the shared command slot busy through direct result callbacks.
      emit statisticsReady(presentation);
      if (!guard) { return; }
      completionMonitor_->stop();
      busy_ = false;
      emit busyChanged(false);
    }, Qt::QueuedConnection);
  }, false); // Optional statistics failures never query/revise the library.
}
ui::ManualTagStatisticsPresentation SessionBridge::manualTagPacket() {
  if (!host_ || !host_->manualTagsEnabled || !host_->revision || !host_->manualTagRevision
      || host_->manualTagSequence == std::numeric_limits<quint64>::max()) { return {}; }
  auto& host = *host_;
  if (host.manualTagReadState == ui::ManualTagReadState::Ready
      && (host.manualResolvedLibrary != host.revision || host.manualResolvedTag != host.manualTagRevision)) {
    host.manualMembership.reset(); host.manualJoinFailure.clear();
    host.manualResolvedLibrary = host.revision; host.manualResolvedTag = host.manualTagRevision;
    if (!host.lastSnapshot || host.lastSnapshot->revision != host.revision
        || host.currentHashes.size() > core::MaximumManualTagLibraryHashes || !host.manualTagSource) {
      host.manualJoinFailure = tr("The current library is unavailable for manual tags");
    } else {
      std::vector<core::TorrentHash> hashes; hashes.reserve(host.currentHashes.size());
      for (const auto& key : host.currentHashes) {
        const auto raw = QByteArray::fromHex(QByteArray::fromStdString(key));
        const auto hash = core::TorrentHash::parse(std::string_view(raw.constData(), static_cast<std::size_t>(raw.size())));
        if (!hash) { host.manualJoinFailure = tr("Invalid current library identity"); break; }
        hashes.push_back(*hash);
      }
      if (host.manualJoinFailure.isEmpty()) {
        auto joined = core::resolveManualTagMembership(host.columnLayoutSession, host.revision,
            host.manualTagRevision, *host.manualTagSource, hashes);
        if (joined) { host.manualMembership = std::move(*joined); }
        else { host.manualJoinFailure = tr("Cannot resolve bounded manual tag membership"); }
      }
    }
  }
  auto packet = std::make_shared<ui::ManualTagStatisticsPacket>();
  packet->session = host.columnLayoutSession; packet->epoch = host.manualOwnerEpoch;
  packet->libraryRevision = host.revision; packet->tagRevision = host.manualTagRevision;
  packet->readState = host.manualTagReadState; packet->failure = host.manualTagFailure;
  if (packet->readState == ui::ManualTagReadState::Ready) {
    if (!host.manualMembership) {
      packet->readState = ui::ManualTagReadState::Failed;
      packet->failure = host.manualJoinFailure.isEmpty() ? tr("Manual tag membership is unavailable") : host.manualJoinFailure;
    } else {
      packet->membership = host.manualMembership;
      if (host.manualTagEnded) {
        packet->traffic = host.manualLastTraffic; packet->traffic.current = false;
      } else if (host.engine && host.manualOwnerSampled
          && host.manualOwnerState == engine::AccountingState::Idle && host.manualIdleAuthority && !host.manualTagCommandActive) {
        // Actual owner Idle means no registered handle in the one-owner source.
        packet->traffic = {core::ManualTagTrafficState::Idle, {}, true, true};
      } else if (host.engine && host.manualTagCommandActive && host.manualTarget
          && host.manualOwnerState == engine::AccountingState::Active && host.manualOwnerSampled) {
        // One admitted owner handle, and this exact handle's same-status rates.
        packet->traffic = {core::ManualTagTrafficState::Active, host.manualTarget, true, true};
      } else if (host.manualOwnerState == engine::AccountingState::Retiring
          || host.manualOwnerState == engine::AccountingState::Closing
          || host.manualOwnerState == engine::AccountingState::Closed
          || host.manualOwnerState == engine::AccountingState::Uncertified) {
        packet->traffic = host.manualLastTraffic; packet->traffic.current = false;
      }
      if (packet->traffic.target && !host.manualMembership->contains(packet->traffic.target->hash)) {
        packet->traffic = {}; // Changed membership never adopts an old target.
      }
      if (packet->traffic.current && packet->traffic.state != core::ManualTagTrafficState::Unavailable) {
        host.manualLastTraffic = packet->traffic;
      }
    }
  }
  if (host.manualTagEnded) { packet->traffic.current = false; }
  packet->sequence = ++host.manualTagSequence;
  return packet;
}
void SessionBridge::deliverManualTags(ui::ManualTagStatisticsPresentation packet) {
  const auto previous = deliveredManualTags_ && packet && deliveredManualTags_->session == packet->session
      ? deliveredManualTags_ : ui::ManualTagStatisticsPresentation{};
  if (!ui::acceptManualTagStatistics(previous, packet, publishedColumnLayoutSession_)) { return; }
  deliveredManualTags_ = packet;
  emit manualTagStatisticsReady(std::move(packet)); // Last access: direct receiver may delete Bridge.
}
void SessionBridge::publishManualTags() noexcept {
  try {
    auto packet = manualTagPacket(); if (!packet) { return; }
    {
      const std::lock_guard lock(progressMutex_);
      latestManualTags_ = std::move(packet);
      if (manualTagDeliveryQueued_) { return; }
      manualTagDeliveryQueued_ = true;
    }
    if (QMetaObject::invokeMethod(this, [this] {
      std::optional<ui::ManualTagStatisticsPresentation> packet;
      {
        const std::lock_guard lock(progressMutex_);
        packet.swap(latestManualTags_); manualTagDeliveryQueued_ = false;
      }
      if (packet) { deliverManualTags(std::move(*packet)); }
    }, Qt::QueuedConnection)) { return; }
    const std::lock_guard lock(progressMutex_); manualTagDeliveryQueued_ = false;
  } catch (...) {
    // All parser/join/UI allocation failures are optional, never transfer failures.
    const std::lock_guard lock(progressMutex_); manualTagDeliveryQueued_ = false;
  }
}
ui::CountryStatisticsPresentation SessionBridge::countryPacket() {
  if (!host_ || !host_->countriesEnabled || host_->countrySequence == std::numeric_limits<quint64>::max()) { return {}; }
  auto& host = *host_; bool current = false;
  if (host.engine) {
    // Owned scalar copy from the existing passive observer, never another pump.
    const auto ledger = host.engine->peerPayloads();
    if (ledger && ledger->identity.profileSession == host.columnLayoutSession) {
      const auto projected = host.countryProvider ? host.countryProvider->snapshot(*ledger)
          : core::projectCountryStatistics(*ledger, {}, false);
      if (projected) {
        host.lastCountries = std::make_shared<const core::CountryStatistics>(*projected);
        current = !host.countryEnded && (host.manualOwnerState == engine::AccountingState::Idle
            || host.manualOwnerState == engine::AccountingState::Active);
      }
    }
  }
  auto packet = std::make_shared<ui::CountryStatisticsPacket>();
  packet->session = host.columnLayoutSession; packet->sequence = ++host.countrySequence;
  packet->snapshot = host.lastCountries; packet->epoch = packet->snapshot ? packet->snapshot->identity.engineEpoch : 0;
  packet->current = current; return packet;
}
void SessionBridge::deliverCountries(ui::CountryStatisticsPresentation packet) {
  const auto previous = deliveredCountries_ && packet && deliveredCountries_->session == packet->session
      ? deliveredCountries_ : ui::CountryStatisticsPresentation{};
  if (!ui::acceptCountryStatistics(previous, packet, publishedColumnLayoutSession_)) { return; }
  deliveredCountries_ = packet;
  emit countryStatisticsReady(std::move(packet)); // Final access; receiver may delete Bridge.
}
void SessionBridge::publishCountries() noexcept {
  try {
    auto packet = countryPacket(); if (!packet) { return; }
    {
      const std::lock_guard lock(progressMutex_); latestCountries_ = std::move(packet);
      if (countryDeliveryQueued_) { return; } countryDeliveryQueued_ = true;
    }
    if (QMetaObject::invokeMethod(this, [this] {
      std::optional<ui::CountryStatisticsPresentation> packet;
      {
        const std::lock_guard lock(progressMutex_); packet.swap(latestCountries_); countryDeliveryQueued_ = false;
      }
      if (packet) { deliverCountries(std::move(*packet)); }
    }, Qt::QueuedConnection)) { return; }
    const std::lock_guard lock(progressMutex_); countryDeliveryQueued_ = false;
  } catch (...) {
    // Optional geography, ledger copy and GUI allocations never fail a transfer.
    const std::lock_guard lock(progressMutex_); countryDeliveryQueued_ = false;
  }
}
bool SessionBridge::countryStatistics(quint64 session) {
  if (!session || session != publishedColumnLayoutSession_) { return false; }
  return dispatch([this, session](std::stop_token stop) {
    if (!host_ || host_->columnLayoutSession != session) {
      throw Failure{{storage::ErrorCode::Conflict, "This country statistics session is no longer active"}};
    }
    checkpoint(stop); auto& host = *host_; host.countriesEnabled = true;
    if (!host.countryProviderAttempted) {
      host.countryProviderAttempted = true;
      try { if (countryProviderFactory_) { host.countryProvider = countryProviderFactory_(host.lease); } }
      catch (...) {} // Authentication/provider failure remains unavailable, never guessed.
    }
    auto packet = countryPacket(); checkpoint(stop);
    QMetaObject::invokeMethod(this, [this, packet = std::move(packet), stop] {
      QPointer<SessionBridge> guard(this); if (!stop.stop_requested()) { deliverCountries(packet); }
      if (!guard) { return; } completionMonitor_->stop(); busy_ = false; emit busyChanged(false);
      if (guard && stop.stop_requested()) { emit messageReady(tr("Cancelled")); }
    }, Qt::QueuedConnection);
  }, false);
}

bool SessionBridge::manualTagStatistics(quint64 session) {
  if (!session || session != publishedColumnLayoutSession_) { return false; }
  return dispatch([this, session](std::stop_token stop) {
    if (!host_ || host_->columnLayoutSession != session) {
      throw Failure{{storage::ErrorCode::Conflict, "This manual tag session is no longer active"}};
    }
    auto& host = *host_;
    if (host.manualTagRevision == std::numeric_limits<quint64>::max()) {
      throw Failure{{storage::ErrorCode::Unsupported, "Manual tag revisions are exhausted"}};
    }
    checkpoint(stop);
    // Every explicit Refresh reads anew; samples only reuse this result.
    host.manualTagsEnabled = true; ++host.manualTagRevision;
    host.manualTagReadState = ui::ManualTagReadState::Failed;
    host.manualTagSource.reset(); host.manualMembership.reset();
    host.manualTagFailure.clear(); host.manualJoinFailure.clear();
    auto view = storage::readSavedDownloadTagsView(host.lease, stop);
    checkpoint(stop);
    if (!view) {
      if (view.error().code == storage::ErrorCode::Cancelled) { throw Failure{view.error()}; }
      host.manualTagFailure = text(view.error().message);
    } else if (view->missing()) {
      host.manualTagReadState = ui::ManualTagReadState::Missing;
    } else if (!view->snapshot || !view->recoverySource) {
      host.manualTagFailure = tr("Manual tag source is unavailable");
    } else {
      host.manualTagSource = std::make_shared<const core::SavedDownloadTags>(std::move(*view->snapshot));
      host.manualTagReadState = ui::ManualTagReadState::Ready;
    }
    auto packet = manualTagPacket(); checkpoint(stop);
    QMetaObject::invokeMethod(this, [this, packet = std::move(packet), stop] {
      QPointer<SessionBridge> guard(this);
      if (!stop.stop_requested()) { deliverManualTags(packet); }
      if (!guard) { return; }
      completionMonitor_->stop(); busy_ = false; emit busyChanged(false);
      if (guard && stop.stop_requested()) { emit messageReady(tr("Cancelled")); }
    }, Qt::QueuedConnection);
  }, false);
}
bool SessionBridge::history(quint64 session, quint64 request, core::HistoryDate date) {
  if (session == 0 || request == 0 || session != publishedColumnLayoutSession_
      || !core::validHistoryDate(date)) { return false; }
  return dispatch([this, session, request, date](std::stop_token stop) {
    if (!host_ || host_->columnLayoutSession != session) {
      throw Failure{{storage::ErrorCode::Conflict, "This history session is no longer active"}};
    }
    auto packet = std::make_shared<ui::HistoryPacket>();
    packet->session = session; packet->request = request; packet->date = date;
    checkpoint(stop);
    auto view = storage::readSavedHistoryDayView(host_->lease, date, stop);
    checkpoint(stop);
    if (!view) {
      if (view.error().code == storage::ErrorCode::Cancelled) { throw Failure{std::move(view.error())}; }
      packet->failure = text(view.error().message);
    } else if (view->missing()) {
      packet->state = ui::HistoryReadState::Missing;
    } else {
      packet->snapshot = std::make_shared<const core::SavedHistoryDay>(std::move(*view->snapshot));
      packet->state = ui::HistoryReadState::Ready;
    }
    checkpoint(stop);
    ui::HistoryPresentation presentation = std::move(packet);
    QMetaObject::invokeMethod(this, [this, presentation = std::move(presentation), stop] {
      QPointer<SessionBridge> guard(this);
      if (stop.stop_requested() || presentation->session != publishedColumnLayoutSession_) {
        completionMonitor_->stop(); busy_ = false; emit busyChanged(false);
        if (guard && stop.stop_requested()) { emit messageReady(tr("Cancelled")); }
        return;
      }
      // Retain the shared command slot through all direct result callbacks.
      emit historyReady(presentation);
      if (!guard) { return; }
      completionMonitor_->stop(); busy_ = false; emit busyChanged(false);
    }, Qt::QueuedConnection);
  }, false); // A read-only history failure cannot revise the saved library.
}
bool SessionBridge::historyWeek(quint64 session, quint64 request, core::HistoryDate date) {
  if (session == 0 || request == 0 || session != publishedColumnLayoutSession_
      || !core::savedHistoryWeekDates(date)) { return false; }
  return dispatch([this, session, request, date](std::stop_token stop) {
    if (!host_ || host_->columnLayoutSession != session) {
      throw Failure{{storage::ErrorCode::Conflict, "This history session is no longer active"}};
    }
    auto packet = std::make_shared<ui::HistoryWeekPacket>();
    packet->session = session; packet->request = request; packet->endingDate = date;
    checkpoint(stop);
    auto view = storage::readSavedHistoryWeekView(host_->lease, date, stop);
    checkpoint(stop);
    if (!view) {
      if (view.error().code == storage::ErrorCode::Cancelled) { throw Failure{std::move(view.error())}; }
      packet->failure = text(view.error().message);
    } else {
      packet->snapshot = std::make_shared<const core::SavedHistoryWeek>(std::move(*view));
      packet->state = ui::HistoryWeekReadState::Ready;
    }
    checkpoint(stop);
    ui::HistoryWeekPresentation presentation = std::move(packet);
    QMetaObject::invokeMethod(this, [this, presentation = std::move(presentation), stop] {
      QPointer<SessionBridge> guard(this);
      if (stop.stop_requested() || presentation->session != publishedColumnLayoutSession_) {
        completionMonitor_->stop(); busy_ = false; emit busyChanged(false);
        if (guard && stop.stop_requested()) { emit messageReady(tr("Cancelled")); }
        return;
      }
      emit historyWeekReady(presentation);
      if (!guard) { return; }
      completionMonitor_->stop(); busy_ = false; emit busyChanged(false);
    }, Qt::QueuedConnection);
  }, false); // Optional history never queries or revises the saved Library.
}
bool SessionBridge::historyMonth(quint64 session, quint64 request, core::HistoryDate date) {
  if (session == 0 || request == 0 || session != publishedColumnLayoutSession_
      || !core::savedHistoryMonthDates(date)) { return false; }
  return dispatch([this, session, request, date](std::stop_token stop) {
    if (!host_ || host_->columnLayoutSession != session) {
      throw Failure{{storage::ErrorCode::Conflict, "This history session is no longer active"}};
    }
    auto packet = std::make_shared<ui::HistoryMonthPacket>();
    packet->session = session; packet->request = request; packet->endingDate = date;
    checkpoint(stop);
    auto view = storage::readSavedHistoryMonthView(host_->lease, date, stop);
    checkpoint(stop);
    if (!view) {
      if (view.error().code == storage::ErrorCode::Cancelled) { throw Failure{std::move(view.error())}; }
      packet->failure = text(view.error().message);
    } else {
      packet->snapshot = std::make_shared<const core::SavedHistoryMonth>(std::move(*view));
      packet->state = ui::HistoryMonthReadState::Ready;
    }
    checkpoint(stop);
    ui::HistoryMonthPresentation presentation = std::move(packet);
    QMetaObject::invokeMethod(this, [this, presentation = std::move(presentation), stop] {
      QPointer<SessionBridge> guard(this);
      if (stop.stop_requested() || presentation->session != publishedColumnLayoutSession_) {
        completionMonitor_->stop(); busy_ = false; emit busyChanged(false);
        if (guard && stop.stop_requested()) { emit messageReady(tr("Cancelled")); }
        return;
      }
      emit historyMonthReady(presentation);
      if (!guard) { return; }
      completionMonitor_->stop(); busy_ = false; emit busyChanged(false);
    }, Qt::QueuedConnection);
  }, false); // Optional history never queries or revises the saved Library.
}
bool SessionBridge::historyRange(quint64 session, quint64 request, core::HistoryRangeRequest query) {
  if (session == 0 || request == 0 || session != publishedColumnLayoutSession_
      || !core::planSavedHistoryRange(query)) { return false; }
  return dispatch([this, session, request, query](std::stop_token stop) {
    if (!host_ || host_->columnLayoutSession != session) {
      throw Failure{{storage::ErrorCode::Conflict, "This history session is no longer active"}};
    }
    auto packet = std::make_shared<ui::HistoryRangePacket>();
    packet->session = session; packet->request = request; packet->query = query;
    checkpoint(stop);
    auto view = storage::readSavedHistoryRangeView(host_->lease, query, stop);
    checkpoint(stop);
    if (!view) {
      if (view.error().code == storage::ErrorCode::Cancelled) { throw Failure{std::move(view.error())}; }
      packet->failure = text(view.error().message);
    } else {
      packet->snapshot = std::make_shared<const core::SavedHistoryRange>(std::move(*view));
      packet->state = ui::HistoryRangeReadState::Ready;
    }
    checkpoint(stop);
    ui::HistoryRangePresentation presentation = std::move(packet);
    QMetaObject::invokeMethod(this, [this, presentation = std::move(presentation), stop] {
      QPointer<SessionBridge> guard(this);
      if (stop.stop_requested() || presentation->session != publishedColumnLayoutSession_) {
        completionMonitor_->stop(); busy_ = false; emit busyChanged(false);
        if (guard && stop.stop_requested()) { emit messageReady(tr("Cancelled")); }
        return;
      }
      emit historyRangeReady(presentation);
      if (!guard) { return; }
      completionMonitor_->stop(); busy_ = false; emit busyChanged(false);
    }, Qt::QueuedConnection);
  }, false); // Optional history never queries or revises the saved Library.
}
bool SessionBridge::columnLayouts(quint64 session, quint64 request) {
  if (session == 0 || request == 0 || session != publishedColumnLayoutSession_) { return false; }
  return dispatch([this, session, request](std::stop_token stop) {
    if (!host_ || host_->columnLayoutSession != session) {
      throw Failure{{storage::ErrorCode::Conflict, "This column layout session is no longer active"}};
    }
    auto packet = std::make_shared<ui::ColumnLayoutsPacket>();
    packet->session = session;
    packet->request = request;
    checkpoint(stop);
    host_->columnBaseline.reset();
    host_->columnBaselineSource.reset();
    host_->columnBaselineMissing = false;
    host_->columnBaselineReadStop = stop;
    auto view = storage::readColumnLayoutsView(host_->lease, stop);
    checkpoint(stop);
    if (!view) {
      if (view.error().code == storage::ErrorCode::Cancelled) { throw Failure{std::move(view.error())}; }
      packet->failure = text(view.error().message);
    } else if (view->missing()) {
      host_->columnBaselineMissing = true;
      packet->state = ui::ColumnLayoutReadState::Missing;
    } else if (!view->recoverySource || !(packet->source = columnLayoutSource(*view->recoverySource))) {
      packet->failure = tr("Saved column layout source is unavailable");
    } else {
      packet->snapshot = std::make_shared<const core::ColumnLayouts>(std::move(*view->snapshot));
      packet->nativePreferences = std::move(view->nativePreferences);
      host_->columnBaseline = packet->snapshot->source;
      host_->columnBaselineSource = view->recoverySource;
      packet->state = ui::ColumnLayoutReadState::Ready;
    }
    checkpoint(stop);
    ui::ColumnLayoutsPresentation presentation = std::move(packet);
    QMetaObject::invokeMethod(this, [this, presentation = std::move(presentation), stop] {
      QPointer<SessionBridge> guard(this);
      if (stop.stop_requested() || presentation->session != publishedColumnLayoutSession_) {
        completionMonitor_->stop();
        busy_ = false;
        emit busyChanged(false);
        if (guard && stop.stop_requested()) { emit messageReady(tr("Cancelled")); }
        return;
      }
      // Hold busy through direct result slots, including cancellation/deletion.
      emit columnLayoutsReady(presentation);
      if (!guard) { return; }
      completionMonitor_->stop();
      busy_ = false;
      emit busyChanged(false);
    }, Qt::QueuedConnection);
  }, false);
}
bool SessionBridge::saveColumnLayout(ui::ColumnLayoutSaveSubmission submission) {
  if (!submission || submission->session == 0 || submission->request == 0
      || submission->session != publishedColumnLayoutSession_ || !submission->preferences
      || !core::validateClassicPreferences(*submission->preferences)) { return false; }
  // Copy bounded input before worker dispatch; the worker never borrows GUI data.
  const auto session = submission->session;
  const auto request = submission->request;
  auto preferences = std::make_shared<const core::ClassicPreferences>(*submission->preferences);
  return dispatch([this, session, request, preferences = std::move(preferences)](std::stop_token stop) {
    auto packet = std::make_shared<ui::ColumnLayoutSavePacket>();
    packet->session = session;
    packet->request = request;
    checkpoint(stop);
    if (!host_ || host_->fileRecoveryRequired || host_->columnLayoutSession != session || host_->columnBaselineReadStop.stop_requested()
        || (!host_->columnBaselineMissing && (!host_->columnBaseline || !host_->columnBaselineSource))) {
      packet->failure = tr("Reload the saved column layout before saving");
    } else {
      const auto result = host_->columnBaselineMissing
          ? storage::createClassicPreferencesView(host_->lease, *preferences, stop)
          : storage::saveClassicPreferencesView(host_->lease,
              *host_->columnBaseline, *host_->columnBaselineSource, *preferences, stop);
      // No cancellation checkpoint here: save may already have replaced primary.
      if (result) {
        host_->columnBaseline = *result;
        host_->columnBaselineSource = storage::RecoverySource::Primary;
        host_->columnBaselineMissing = false;
        host_->columnBaselineReadStop = {};
        packet->saved = true;
      } else {
        host_->columnBaseline.reset();
        host_->columnBaselineSource.reset();
        host_->columnBaselineMissing = false;
        packet->failure = text(result.error().message);
      }
    }
    ui::ColumnLayoutSavePresentation presentation = std::move(packet);
    QMetaObject::invokeMethod(this, [this, presentation = std::move(presentation)] {
      QPointer<SessionBridge> guard(this);
      // Always deliver the actual write result, including after cancellation.
      emit columnLayoutSaved(presentation);
      if (!guard) { return; }
      completionMonitor_->stop();
      busy_ = false;
      emit busyChanged(false);
    }, Qt::QueuedConnection);
  }, false);
}
bool SessionBridge::details(ui::Target target) {
  if (profileClosed_) { return false; }
  return dispatch([this, target = std::move(target)](std::stop_token stop) {
    if (!host_) { throw Failure{{storage::ErrorCode::Missing, "No profile is open"}}; }
    host_->accepts(target);
    const auto value = take(engine::queryTorrent(host_->lease, identity(target)));
    checkpoint(stop);
    auto name = text(value.displayName && !value.displayName->empty() ? *value.displayName : value.name);
    if (const auto known = host_->known.find(target.hash.toStdString()); known != host_->known.end() && !known->second.name.isEmpty()) {
      name = known->second.name; // The original cache can own a newer display name.
    }
    host_->known.insert_or_assign(target.hash.toStdString(), Host::Known{name, value.bytes});
    auto files = std::make_shared<ui::FileDetails>();
    files->target = target;
    files->canEdit = host_->engine && host_->engine->managedAvailable() && !host_->fileRecoveryRequired && !storage::hasPendingOriginalCompaction(host_->lease);
    if (host_->engine) {
      for (const auto& active : host_->engine->managedDownloads()) {
        if (active.hash == identity(target) && (active.failure || active.phase==engine::ManagedPhase::Stopping)) { files->canEdit = false; }
      }
    }
    files->canCompact = host_->compactionReady();
    std::vector<bool> compactable(value.entries.size(), false);
    if (files->canCompact) {
      // Read-only source/default resolution; the mutating primitive repeats all
      // geometry/control/path/alias checks under this same worker-held lease.
      auto store = take(storage::TorrentStateStore::open(host_->lease));
      const auto state = take(store.load(host_->lease, identity(target)));
      const auto layout = storage::readOriginalLayout(host_->lease,state.document());
      const auto* attributes = state.document().find("attributes");
      const auto empty = core::Document::parse("de");
      if (layout && empty) {
        const auto kinds = storage::resolveOriginalStorageTypes(host_->lease, identity(target), *layout,
            attributes ? *attributes : empty->root());
        if (kinds && !kinds->unsupportedConversion && kinds->actual.size() == compactable.size()) {
          for (std::size_t index = 0; index < compactable.size(); ++index) {
            compactable[index] = kinds->actual[index] == storage::OriginalStorageKind::Linear
                && kinds->requested[index] == storage::OriginalStorageKind::Linear;
          }
        }
      }
    }
    files->items.reserve(value.entries.size());
    for (const auto& file : value.entries) {
      files->items.push_back({file.index, text(file.path), file.bytes, file.priority, file.skipped, file.padding,
          !file.padding && compactable[static_cast<std::size_t>(file.index)]});
    }
    QMetaObject::invokeMethod(this, [this, result = ui::Details{target, name, value.bytes, std::move(files)}] {
      QPointer<SessionBridge> guard(this);
      completionMonitor_->stop();
      busy_ = false;
      emit busyChanged(false);
      if (!guard) { return; }
      emit detailsReady(result);
    }, Qt::QueuedConnection);
  });
}
void SessionBridge::progress(ui::Telemetry update) {
  {
    const std::lock_guard lock(progressMutex_);
    // Coalesce per hash: one busy torrent must not erase another torrent's
    // progress. The model revision still rejects old-session deliveries.
    const auto key = update.target.hash;
    if (latestProgress_.size() >= ui::LibraryModel::MaximumRows && !latestProgress_.contains(key)) { return; }
    latestProgress_.insert_or_assign(key, std::move(update));
    if (deliveryQueued_) { return; }
    deliveryQueued_ = true;
  }
  QMetaObject::invokeMethod(this, [this] {
    std::map<QString, ui::Telemetry> updates;
    {
      const std::lock_guard lock(progressMutex_);
      updates.swap(latestProgress_);
      deliveryQueued_ = false;
    }
    QPointer<SessionBridge> guard(this);
    for (const auto& [key, update] : updates) {
      static_cast<void>(key); emit progressReady(update); if (!guard) { return; }
    }
  }, Qt::QueuedConnection);
}
void SessionBridge::engineStatistics(ui::EngineStatisticsPresentation packet) {
  if (packet->state != ui::EngineStatisticsState::Observed) {
    // Lifecycle boundaries are never coalesced with measurements.
    QMetaObject::invokeMethod(this, [this, packet = std::move(packet)] {
      if (packet->session == publishedColumnLayoutSession_) { emit engineStatisticsReady(packet); }
    }, Qt::QueuedConnection);
    return;
  }
  {
    const std::lock_guard lock(progressMutex_);
    latestEngineStatistics_ = std::move(packet);
    if (engineDeliveryQueued_) { return; }
    engineDeliveryQueued_ = true;
  }
  QMetaObject::invokeMethod(this, [this] {
    std::optional<ui::EngineStatisticsPresentation> packet;
    {
      const std::lock_guard lock(progressMutex_);
      packet.swap(latestEngineStatistics_);
      engineDeliveryQueued_ = false;
    }
    if (packet && (*packet)->session == publishedColumnLayoutSession_) { emit engineStatisticsReady(*packet); }
  }, Qt::QueuedConnection);
}
bool SessionBridge::continueDownload(ui::Target target) {
  if (profileClosed_) { return false; }
  return dispatch([this, target = std::move(target)](std::stop_token stop) {
    if (!host_) { throw Failure{{storage::ErrorCode::Missing, "No profile is open"}}; }
    host_->accepts(target);
    if(host_->fileRecoveryRequired){throw Failure{{storage::ErrorCode::Conflict,"Profile recovery must finish before starting a transfer"}};}
    if (host_->engineOperation == std::numeric_limits<quint64>::max()) {
      throw Failure{{storage::ErrorCode::Unsupported, "Engine operation identities are exhausted"}};
    }
    // The accepted snapshot also names downloads without a torrent-cache entry.
    // Reuse that owned label; do not read torrent files just for telemetry.
    QString name;
    if (host_->lastSnapshot) {
      for (const auto& row : host_->lastSnapshot->items) {
        if (row.hash == target.hash) { name = row.name; break; }
      }
    }
    ui::EngineStatisticsPacket packet{.session = host_->columnLayoutSession,
        .operation = ++host_->engineOperation, .sequence = 1, .target = target,
        .name = std::move(name)};
    engineStatistics(std::make_shared<const ui::EngineStatisticsPacket>(packet));
    const auto ended = [&](bool successful) {
      ++packet.sequence;
      packet.state = ui::EngineStatisticsState::Ended;
      packet.successful = successful;
      engineStatistics(std::make_shared<const ui::EngineStatisticsPacket>(packet));
    };
    if (host_->peerPort == 0) {
      ended(false);
      throw Failure{{storage::ErrorCode::Unsupported, "Transfers are disabled in this offline profile"}};
    }
    try { initializePersistentOwner(stop); }
    catch (...) { ended(false); throw; }
    host_->manualTagCommandActive = true; host_->manualTarget.reset(); host_->manualIdleAuthority = false;
    publishManualTags();
    const auto finishTags = [this](Host* host) noexcept {
      if (host_.get() != host) { return; }
      host->manualTagCommandActive = false; host->manualTarget.reset(); publishManualTags();
    };
    const std::unique_ptr<Host, decltype(finishTags)> tagCommand(host_.get(), finishTags);
    auto options = engine::LocalOptions{.profile = host_->profile, .hash = identity(target),
        .peerPort = host_->peerPort, .stop = stop};
    options.progress = [this, target, commandHash = options.hash](const engine::LocalProgress& value) {
      if (host_ && host_->manualTagCommandActive) {
        host_->manualTarget = value.targetTraffic && value.targetTraffic->hash == commandHash
            && core::validTargetTraffic(*value.targetTraffic) ? value.targetTraffic : std::nullopt;
        // Fixed owned primitives are retained before optional UI allocation.
        publishManualTags();
      }
      const auto state = value.phase == engine::LocalProgress::Phase::Checking ? tr("Checking")
          : value.phase == engine::LocalProgress::Phase::Downloading ? tr("Downloading") : tr("Finishing");
      progress({target, value.completedBytes, value.downloadRate, value.uploadRate, state});
    };
    options.statistics = [&](const core::EngineStatistics& value) {
      ++packet.sequence;
      packet.state = ui::EngineStatisticsState::Observed;
      packet.sample = value;
      engineStatistics(std::make_shared<const ui::EngineStatisticsPacket>(packet));
    };
    const auto report = [&] {
      try {
        return host_->engine ? take(engine::continueLocal(*host_->engine, host_->lease, options))
            : take(engine::continueLocal(host_->lease, options));
      }
      catch (...) { ended(false); throw; }
    }();
    ended(true);
    auto& known = host_->known[target.hash.toStdString()];
    known.bytes = report.bytes;
    if (known.name.isEmpty()) { known.name = text(report.payload.filename().string()); }
    ready(host_->query({}), tr("Complete"));
  });
}
void SessionBridge::initializePersistentOwner(std::stop_token stop) {
  if (!host_ || host_->fileRecoveryRequired || (host_->peerPort == 0 && !host_->normalNetwork) || host_->engine) { return; }
  checkpoint(stop);
  // Preserve the exact source/runtime guard and default unbound fallback.
  auto owner = engine::PersistentSession::create({1, host_->columnLayoutSession, 1});
  if (!owner) {
    if (owner.error() == engine::AccountingError::DependencyUnqualified) { return; }
    throw Failure{{storage::ErrorCode::Io, "Cannot create the native engine owner"}};
  }
  if (host_->normalNetwork) {
    if (!host_->socks4CapabilityChecked) {
      // Retain actual provider proof before parsing any possibly invalid saved
      // setting. Options can then repair that setting without starting routes.
      host_->networkPolicy.socks4Capability = engine::verifiedSocks4Capability();
      host_->socks4CapabilityChecked = true;
    }
    auto config = storage::ConfigFile::load(host_->lease, "biglybt.config");
    if (!config && config.error().code != storage::ErrorCode::Missing) { throw Failure{config.error()}; }
    auto policy = engine::readNetworkPolicy(config ? &config->document() : nullptr,
        {.mode = engine::NetworkMode::Normal, .socks4Capability = host_->networkPolicy.socks4Capability});
    if (!policy) { throw Failure{{storage::ErrorCode::Unsupported, "Unsupported network setting: " + policy.error().field}}; }
    auto queues = core::readQueuePolicy(config ? &config->document() : nullptr);
    if (!queues) { throw Failure{{storage::ErrorCode::InvalidFormat,
        "Invalid queue setting: " + queues.error().key}}; }
    auto configuredQueue = (*owner)->configureQueuePolicy(*queues,configuredSourceSmoothing(config ? &config->document() : nullptr).windowSeconds);
    if (!configuredQueue) { throw Failure{configuredQueue.error()}; }
    auto applied = (*owner)->configureNetwork(*policy);
    if (!applied) { throw Failure{applied.error()}; }
    host_->networkPolicy = std::move(*policy);
    auto live = (*owner)->applyLiveSettings(host_->networkPolicy, config ? optionQueues(config->document()) : engine::LiveQueueLimits{});
    if (!live) { throw Failure{live.error()}; }
  }
  if (!(*owner)->startIdle(host_->lease)) {
    throw Failure{{storage::ErrorCode::Io, "Cannot initialize the native idle session"}};
  }
  checkpoint(stop); // Local owning pointer aborts/joins before a cancelled publication.
  host_->engine = std::move(*owner);
  try { host_->liveSpeedMapper = std::make_unique<LiveSpeedMapperBinding>(host_->columnLayoutSession); } catch (...) {}
  // Optional accounting setup cannot change transfer/storage success.
  try {
    const auto started = wallMillis();
    host_->lastAccountingTick = std::chrono::steady_clock::now();
    auto settings = storage::ConfigFile::load(host_->lease, "biglybt.config");
    if (settings) {
      if (const auto* enabled = settings->document().find("long.term.stats.enable")) {
        if (enabled->type == core::Node::Type::Integer) { host_->historyEnabled = enabled->integer != 0; }
        else {
          host_->historyEnabled = false;
          host_->historyFailure = storage::Error{storage::ErrorCode::InvalidFormat, "Invalid long.term.stats.enable setting"};
        }
      }
    } else if (settings.error().code != storage::ErrorCode::Missing) {
      host_->historyEnabled = false; host_->historyFailure = std::move(settings.error());
    }
    auto service = OverallStatisticsService::open(host_->lease, {1, host_->columnLayoutSession, 1}, started, wallMillis());
    if (service) { host_->overall = std::move(*service); }
    else { host_->overallOpenFailure = std::move(service.error()); }
    const auto smoothing = configuredSourceSmoothing(settings ? &settings->document() : nullptr);
    host_->sourceRates = std::make_unique<SourceRateProjection>(smoothing.windowSeconds);
  } catch (...) {} // Owner retention is independent of optional allocations.
  static_cast<void>(host_->engine->setObservationSink([this](engine::ApplicationPeerObservation value) {
    observeNative(value);
  }));
  // The existing idle monitor is the only startup poller. Availability and
  // zeros are published only by actual session_stats responses after GUI open.
}
void SessionBridge::observeNative(const engine::ApplicationPeerObservation& value) noexcept {
  if (!host_) { return; }
  // Fixed essential retention precedes every optional allocation/publication.
  if (value.observed && host_->overall) {
    static_cast<void>(host_->overall->capture({ownerIdentity(value.identity), value.sequence,
        peerCounters(value.observed->counters), OverallSampleQuality::Observed}));
  }
  if (value.observed && (!host_->lastCounterSample
      || value.observed->elapsedNanoseconds > host_->lastCounterSample->elapsedNanoseconds)) {
    host_->lastCounterSample = value.observed;
    host_->lastCounterArrival = std::chrono::steady_clock::now();
  }
  publishAzureus(); // Passive actor mailbox; never submits a probe or starts a service.
  if (host_->liveSpeedMapper) {
    host_->liveSpeedMapper->observeEngine(value, steadyMillis()); publishLiveSpeed();
  }
  if (host_->sourceRates) {
    host_->latestSourceRates = host_->sourceRates->observe(value.observed, value.ownerElapsedNanoseconds, value.sequence);
  }
  if (value.state == engine::AccountingState::Uncertified && host_->overall && !host_->overall->uncertified()) {
    // Save the retained observed prefix before latching the missing-tail state.
    tickAccounting(true);
    static_cast<void>(host_->overall->invalidate(ownerIdentity(value.identity)));
  }
  tickAccounting();
  bool countryDue = false;
  try {
    const auto now = std::chrono::steady_clock::now();
    if (now - host_->lastOverallPublication >= std::chrono::seconds{1}) {
      countryDue = true;
      host_->lastOverallPublication = now; publishOverall(); publishRuntimeMetrics(value);
    }
  } catch (...) {} // Optional UI cannot disconnect the essential sink.
  if (value.identity.profileSession == host_->columnLayoutSession && value.identity.engineEpoch
      && value.identity.engineEpoch >= host_->manualOwnerEpoch && value.sequence > host_->manualOwnerSequence) {
    host_->manualOwnerEpoch = value.identity.engineEpoch; host_->manualOwnerSequence = value.sequence;
    host_->manualOwnerState = value.state; host_->manualOwnerSampled = value.observed.has_value();
    host_->manualIdleAuthority = value.state == engine::AccountingState::Idle && value.observed.has_value();
    publishManualTags();
  }
  if (countryDue || value.state == engine::AccountingState::Retiring
      || value.state == engine::AccountingState::Closing || value.state == engine::AccountingState::Closed
      || value.state == engine::AccountingState::Uncertified) { publishCountries(); }
  try { nativeSession(nativePacket(value, host_->latestSourceRates)); } catch (...) {}
}
void SessionBridge::refreshDhtAccounting(bool ending) noexcept {
  if (!host_ || !host_->dhtAccounting) { return; }
  try {
    std::optional<AzureusServiceObservation> observation;
    if (host_->azureus) { observation = host_->azureus->observation(); }
    host_->dhtAccounting->observe(observation, ending);
  } catch (...) { host_->dhtAccounting->observe({}, ending); }
}
void SessionBridge::tickAccounting(bool force, bool ending) noexcept {
  if (!host_ || !host_->overall || !host_->dhtAccounting) { return; }
  refreshDhtAccounting(ending);
  const auto dht = host_->dhtAccounting->historyCounters(ending);
  const auto steadyNow = std::chrono::steady_clock::now();
  const bool due = steadyNow - host_->lastAccountingTick >= std::chrono::seconds{60};
  const auto wallNow = wallMillis();
  // History starts on the first actual counter sample, independently of the
  // sixty-second Overall update. Its absolute baseline is the original twelve-
  // entry snapshot plus each actual source difference (Java long arithmetic).
  try {
    if (!ending && host_->historyEnabled && !host_->historyFailure && host_->lastCounterSample) {
      const auto id = core::MinuteHistoryIdentity{1, host_->columnLayoutSession, 1};
      const bool fresh = steadyNow - host_->lastCounterArrival <= std::chrono::seconds{3};
      const auto state = host_->minuteHistory.state();
      std::optional<core::MinuteHistoryRequest> request;
      if (!fresh || !dht || host_->overall->uncertified()) {
        if (state.active) { request = core::MinuteHistoryRequest{core::MinuteHistoryEvent::Suspend, id, wallNow, {}, {}}; }
      } else if (!state.active) {
        auto source = historyPeer(host_->lastCounterSample->counters);
        source[4] = (*dht)[0]; source[5] = (*dht)[1];
        const auto snap = host_->overall->observation().snapshot;
        core::HistoryCounters baseline{};
        for (std::size_t i = 0; i < baseline.size(); ++i) {
          baseline[i] = std::bit_cast<std::int64_t>(static_cast<std::uint64_t>(snap[i])
              + static_cast<std::uint64_t>(source[i]) - static_cast<std::uint64_t>(snap[i + 6]));
        }
        request = core::MinuteHistoryRequest{core::MinuteHistoryEvent::Start, id, wallNow,
            historyInput(host_->lastCounterSample->counters, dht), baseline};
      } else if (due) {
        request = core::MinuteHistoryRequest{core::MinuteHistoryEvent::Tick, id, wallNow,
            historyInput(host_->lastCounterSample->counters, dht), {}};
      }
      if (request) {
        storage::MinuteHistoryWriteReceipt receipt;
        auto written = storage::writeMinuteHistory(host_->lease, host_->minuteHistory, *request, {}, &receipt);
        if (!written) { host_->historyFailure = std::move(written.error()); }
        else if (receipt.appendedActions) { publishHistoryChange(); }
      }
    }
    if (!due && !force) { return; }
    if (due) { host_->lastAccountingTick = steadyNow; }
    auto updated = host_->overall->tick(wallNow, host_->dhtAccounting->overallInput(ending), force);
    if (updated) {
      // Explicit close also saves a ClockGap prefix; this does not reinterpret
      // Java's early-returning force branch as a save intention.
      if (force && updated->disposition == core::OverallUpdateDisposition::ClockGap) {
        static_cast<void>(host_->overall->saveCurrent());
      } else { static_cast<void>(host_->overall->flush()); }
    } else if (ending && force && updated.error() == OverallServiceError::Domain) {
      // The original reducer applies peer deltas before a missing DHT cell.
      // Explicit close persists that known prefix, retaining the previous DHT
      // totals. This is not a certified missing DHT tail or final-counter claim.
      static_cast<void>(host_->overall->saveCurrent());
    }
    try { publishOverall(); } catch (...) {}
  } catch (...) {} // Accounting diagnostics must not fail successful storage.
}
void SessionBridge::publishAzureus() noexcept {
  if (!host_) { return; }
  try {
    std::optional<AzureusServiceObservation> observation;
    if (host_->azureus) { observation = host_->azureus->observation(); }
    if (host_->dhtAccounting) { host_->dhtAccounting->observe(observation); }
    if (host_->liveSpeedMapper) { host_->liveSpeedMapper->observeAzureus(observation, steadyMillis()); }
    publishLiveSpeed();
    if (host_->azureusPublished && (!observation || observation->sequence == host_->azureusObservationSequence)) { return; }
    if (host_->azureusSequence == std::numeric_limits<quint64>::max()) { return; }
    auto packet = std::make_shared<ui::AzureusStatisticsPacket>();
    packet->session = host_->columnLayoutSession; packet->sequence = host_->azureusSequence + 1;
    packet->current = observation && observation->current;
    if (!host_->azureusEnabled) { packet->state = ui::AzureusStatisticsState::Disabled; }
    else if (observation && observation->snapshot) {
      packet->state = ui::AzureusStatisticsState::Observed; packet->snapshot = observation->snapshot;
      packet->routerSnapshot = observation->routerSnapshot;
      packet->lookupSnapshot = observation->lookupSnapshot;
    } else {
      packet->state = ui::AzureusStatisticsState::Unavailable;
      packet->failure = tr("DHT service observations are unavailable");
    }
    {
      const std::lock_guard lock(progressMutex_);
      latestAzureus_ = std::move(packet);
      ++host_->azureusSequence; host_->azureusPublished = true;
      if (observation) { host_->azureusObservationSequence = observation->sequence; }
      if (azureusDeliveryQueued_) { return; }
      azureusDeliveryQueued_ = true;
    }
    QMetaObject::invokeMethod(this, [this] {
      std::optional<ui::AzureusStatisticsPresentation> packet;
      {
        const std::lock_guard lock(progressMutex_);
        packet.swap(latestAzureus_); azureusDeliveryQueued_ = false;
      }
      if (packet && (*packet)->session == publishedColumnLayoutSession_) { emit azureusStatisticsReady(*packet); }
    }, Qt::QueuedConnection);
  } catch (...) {} // Passive optional snapshots never fail storage/transfer commands.
}
void SessionBridge::publishLiveSpeed() noexcept {
  if (!host_) { return; }
  try {
    const auto value = host_->liveSpeedMapper ? host_->liveSpeedMapper->observation() : LiveSpeedMapperObservation{};
    if ((host_->liveSpeedPublished && value.revision == host_->liveSpeedRevision)
        || host_->liveSpeedSequence == std::numeric_limits<quint64>::max()) { return; }
    auto packet = std::make_shared<ui::LiveSpeedManagerPacket>();
    packet->session = host_->columnLayoutSession; packet->sequence = ++host_->liveSpeedSequence;
    packet->snapshot = value.snapshot; packet->epoch = value.snapshot ? value.epoch : 0; packet->current = value.current && bool(value.snapshot);
    switch (value.failure) {
      case LiveSpeedFailure::None: break;
      case LiveSpeedFailure::EngineUnavailable: packet->failure = tr("Native engine observations are unavailable or ended"); break;
      case LiveSpeedFailure::Alignment: packet->failure = tr("RTT and observed rate windows could not be aligned"); break;
      case LiveSpeedFailure::ReceiptLoss: packet->failure = tr("The RTT observation stream has a gap"); break;
      case LiveSpeedFailure::Clock: packet->failure = tr("Observation timing is unavailable"); break;
      case LiveSpeedFailure::Input: packet->failure = tr("A mapper input could not be admitted"); break;
      case LiveSpeedFailure::Memory: packet->failure = tr("The optional mapper snapshot is unavailable"); break;
    }
    {
      const std::lock_guard lock(progressMutex_);
      latestLiveSpeed_ = std::move(packet); host_->liveSpeedRevision = value.revision; host_->liveSpeedPublished = true;
      if (liveSpeedDeliveryQueued_) { return; } liveSpeedDeliveryQueued_ = true;
    }
    QMetaObject::invokeMethod(this, [this] {
      std::optional<ui::LiveSpeedManagerPresentation> result;
      { const std::lock_guard lock(progressMutex_); result.swap(latestLiveSpeed_); liveSpeedDeliveryQueued_ = false; }
      if (result && (*result)->session == publishedColumnLayoutSession_) { emit liveSpeedManagerReady(*result); }
    }, Qt::QueuedConnection);
  } catch (...) {} // Optional mapper publication cannot fail transfer/storage/retirement.
}
void SessionBridge::publishRuntimeMetrics(const engine::ApplicationPeerObservation& value) {
  publishAzureus();
  if (!host_ || !host_->engine) { return; }
  auto packet = std::make_shared<ui::RuntimeMetricsPacket>();
  packet->session = host_->columnLayoutSession; packet->epoch = value.identity.engineEpoch;
  packet->sequence = value.sequence; packet->snapshot = host_->engine->diagnostics();
  if (auto tracker = host_->engine->trackerStatistics()) {
    packet->tracker = std::make_shared<const core::TrackerStatistics>(std::move(*tracker));
  }
  packet->current = value.state == engine::AccountingState::Idle || value.state == engine::AccountingState::Active;
  ui::RuntimeMetricsPresentation result = std::move(packet);
  {
    const std::lock_guard lock(progressMutex_);
    latestRuntime_ = std::move(result);
    if (runtimeDeliveryQueued_) { return; }
    runtimeDeliveryQueued_ = true;
  }
  QMetaObject::invokeMethod(this, [this] {
    std::optional<ui::RuntimeMetricsPresentation> result;
    {
      const std::lock_guard lock(progressMutex_);
      result.swap(latestRuntime_); runtimeDeliveryQueued_ = false;
    }
    if (result && (*result)->session == publishedColumnLayoutSession_) { emit runtimeMetricsReady(*result); }
  }, Qt::QueuedConnection);
}
void SessionBridge::publishHistoryChange() noexcept {
  if (!host_ || host_->historyRevision == std::numeric_limits<quint64>::max()) { return; }
  const HistoryChange change{host_->columnLayoutSession, ++host_->historyRevision};
  try {
    {
      const std::lock_guard lock(progressMutex_);
      latestHistoryChange_ = change;
      if (historyChangeDeliveryQueued_) { return; }
      historyChangeDeliveryQueued_ = true;
    }
    const bool queued = QMetaObject::invokeMethod(this, [this] {
      std::optional<HistoryChange> change;
      {
        const std::lock_guard lock(progressMutex_);
        change.swap(latestHistoryChange_); historyChangeDeliveryQueued_ = false;
      }
      if (change && change->session == publishedColumnLayoutSession_) {
        emit historyChanged(change->session, change->revision); // Last access: receiver may delete Bridge.
      }
    }, Qt::QueuedConnection);
    if (!queued) {
      const std::lock_guard lock(progressMutex_); historyChangeDeliveryQueued_ = false;
    }
  } catch (...) {
    // Optional delivery cannot fail accounting or a successful disk transaction.
    // Retain the latest scalar; the next actual mutation can retry publication.
    const std::lock_guard lock(progressMutex_); historyChangeDeliveryQueued_ = false;
  }
}
void SessionBridge::publishOverall(bool closed) {
  if (!host_ || (!host_->overall && !host_->overallOpenFailure)
      || host_->overallSequence == std::numeric_limits<quint64>::max()) { return; }
  auto packet = std::make_shared<ui::OverallPacket>();
  packet->session = host_->columnLayoutSession; packet->sequence = ++host_->overallSequence;
  packet->closed = closed;
  if (host_->overall) {
    const auto& service = *host_->overall;
    packet->available = true; packet->values = service.values();
    packet->values = service.projectedValues();
    if (service.baseline().snapshot) { packet->saved = service.baseline().snapshot->values; }
    packet->dirty = service.dirty() || !packet->saved || *packet->saved != packet->values;
    packet->writable = service.latestSample().has_value() && !service.writesBlocked() && !service.uncertified() && !closed
        && host_->dhtAccounting && host_->dhtAccounting->historyCounters().has_value();
    packet->finalCounters = service.finalCountersApplied();
    if (const auto* failure = service.lastStorageError()) { packet->failure = text(failure->message); }
    else if (service.writesBlocked()) { packet->failure = tr("Statistics saving is blocked"); }
    else if (service.uncertified()) { packet->failure = tr("The final traffic interval is unavailable; last observed counters retained"); }
  } else { packet->failure = text(host_->overallOpenFailure->message); }
  if (host_->historyFailure) { packet->historyFailure = text(host_->historyFailure->message); }
  if (host_->dhtAccounting && !host_->dhtAccounting->historyCounters(closed)) {
    if (!packet->failure.isEmpty()) { packet->failure += QStringLiteral("; "); }
    packet->failure += tr("DHT byte observations are unavailable; previously recorded totals retained");
    if (packet->historyFailure.isEmpty() && host_->historyEnabled) {
      packet->historyFailure = tr("Saved history is suspended while DHT byte observations are unavailable");
    }
  }
  ui::OverallPresentation result = std::move(packet);
  {
    const std::lock_guard lock(progressMutex_);
    latestOverall_ = std::move(result);
    if (overallDeliveryQueued_) { return; }
    overallDeliveryQueued_ = true;
  }
  QMetaObject::invokeMethod(this, [this] {
    std::optional<ui::OverallPresentation> result;
    {
      const std::lock_guard lock(progressMutex_);
      result.swap(latestOverall_); overallDeliveryQueued_ = false;
    }
    if (result && (*result)->session == publishedColumnLayoutSession_) { emit overallReady(*result); }
  }, Qt::QueuedConnection);
}
bool SessionBridge::markStatistics(quint64 session, bool clear) {
  if (!session || session != publishedColumnLayoutSession_) { return false; }
  return dispatch([this, session, clear](std::stop_token stop) {
    if (!host_ || host_->columnLayoutSession != session || !host_->overall || !host_->dhtAccounting) {
      throw Failure{{storage::ErrorCode::Unsupported, "Live statistics accounting is not active"}};
    }
    checkpoint(stop);
    auto& service = *host_->overall;
    if (service.writesBlocked() || service.uncertified()) {
      throw Failure{{storage::ErrorCode::Conflict, "Statistics saving is blocked; current values are retained"}};
    }
    // Update/stage once, then publish only the complete marked values.
    refreshDhtAccounting();
    auto updated = service.tick(wallMillis(), host_->dhtAccounting->overallInput(), true);
    if (!updated) { throw Failure{{storage::ErrorCode::Conflict, "Current statistics are unavailable"}}; }
    const auto saved = clear ? service.clearMark(stop) : service.setMark(wallMillis(), stop);
    publishOverall();
    if (!saved) {
      if (const auto* failure = service.lastStorageError()) { throw Failure{*failure}; }
      throw Failure{{storage::ErrorCode::Io, "Cannot save the statistics mark"}};
    }
    QMetaObject::invokeMethod(this, [this, clear] {
      QPointer<SessionBridge> guard(this);
      completionMonitor_->stop(); busy_ = false; emit busyChanged(false);
      if (guard) { emit messageReady(clear ? tr("Statistics mark cleared") : tr("Statistics mark saved")); }
    }, Qt::QueuedConnection);
  }, false);
}
void SessionBridge::nativeSession(ui::NativeSessionPresentation packet) {
  if (!packet) { return; }
  {
    const std::lock_guard lock(progressMutex_);
    latestNativeSession_ = std::move(packet);
    if (nativeDeliveryQueued_) { return; }
    nativeDeliveryQueued_ = true;
  }
  QMetaObject::invokeMethod(this, [this] {
    std::optional<ui::NativeSessionPresentation> packet;
    {
      const std::lock_guard lock(progressMutex_);
      packet.swap(latestNativeSession_);
      nativeDeliveryQueued_ = false;
    }
    if (packet && (*packet)->session == publishedColumnLayoutSession_) { emit nativeSessionReady(*packet); }
  }, Qt::QueuedConnection);
}
bool SessionBridge::closeSession() {
  if (busy_ || closingSession_) { return false; }
  closingSession_ = true;
  idleMonitor_->stop();
  QPointer<SessionBridge> guard(this);
  const bool accepted = dispatch([this](std::stop_token) {
    const auto session = host_ ? host_->columnLayoutSession : quint64{0};
    bool joined = true;
    bool released = true;
    ui::NativeSessionPresentation finalObservation;
    QString checkpointFailure;
    if (host_ && host_->azureus) {
      joined = static_cast<bool>(host_->azureus->close());
      publishAzureus();
    }
    refreshDhtAccounting(true); // Actual actor join precedes final accounting and lease release.
    if (host_ && host_->engine) {
      host_->engine->clearObservationSink();
      const auto receipt = host_->engine->close(std::chrono::steady_clock::now() + std::chrono::seconds{5});
      joined = joined && receipt.sessionJoined;
      released = receipt.retirement.storageReleased;
      if (receipt.checkpointFailure) { checkpointFailure = text(receipt.checkpointFailure->message); }
      if (host_->overall && receipt.sessionJoined && released && receipt.retirement.peerCounters) {
        const auto& final = *receipt.retirement.peerCounters;
        static_cast<void>(host_->overall->capture({ownerIdentity(final.identity()), final.sequence(),
            peerCounters(final.counters()), OverallSampleQuality::ClosedFinal}));
        if (host_->minuteHistory.state().active) {
          try {
            storage::MinuteHistoryWriteReceipt historyReceipt;
            auto ended = storage::writeMinuteHistory(host_->lease, host_->minuteHistory,
                {core::MinuteHistoryEvent::End, {1, host_->columnLayoutSession, 1}, wallMillis(), historyInput(final.counters(), host_->dhtAccounting->historyCounters(true)), {}}, {}, &historyReceipt);
            if (!ended && !host_->historyFailure) { host_->historyFailure = std::move(ended.error()); }
            else if (ended && historyReceipt.appendedActions) { publishHistoryChange(); }
          } catch (...) {}
        }
      } else if (host_->minuteHistory.state().active) {
        try { static_cast<void>(storage::writeMinuteHistory(host_->lease, host_->minuteHistory,
            {core::MinuteHistoryEvent::Suspend, {1, host_->columnLayoutSession, 1}, wallMillis(), {}, {}})); } catch (...) {}
      }
      tickAccounting(true, true);
      try { publishOverall(true); } catch (...) {}
      try {
        if (const auto last = host_->engine->latest()) {
          finalObservation = nativePacket(*last); publishRuntimeMetrics(*last);
        }
      } catch (...) {} // Optional immutable telemetry cannot retain Host/lease.
    }
    if (host_) {
      if (host_->liveSpeedMapper) { host_->liveSpeedMapper->close(); } publishLiveSpeed();
      host_->manualTagEnded = true; publishManualTags();
      host_->countryEnded = true; publishCountries();
    }
    host_.reset(); // Required lifetime barrier before GUI close acknowledgement.
    try { nativeSession(std::move(finalObservation)); } catch (...) {}
    QMetaObject::invokeMethod(this, [this, session, joined, released, checkpointFailure] {
      QPointer<SessionBridge> guard(this);
      completionMonitor_->stop();
      publishedColumnLayoutSession_ = 0; // Reject old-session requests before releasing busy.
      profileClosed_ = true;
      busy_ = false;
      emit busyChanged(false); // Keep closingSession_ true through this callback.
      if (!guard) { return; }
      closingSession_ = false;
      idleMonitor_->start();
      if (!checkpointFailure.isEmpty()) { emit messageReady(checkpointFailure); if (!guard) { return; } }
      emit sessionClosed(session, joined, released); // Final access: receiver may reopen or delete Bridge.
    }, Qt::QueuedConnection);
  }, false, true);
  if (!guard) { return accepted; }
  if (!accepted) { closingSession_ = false; idleMonitor_->start(); }
  return accepted;
}
void SessionBridge::closeFailed(QString message) {
  QMetaObject::invokeMethod(this, [this, message = std::move(message)] {
    QPointer<SessionBridge> guard(this);
    closingSession_ = false;
    idleMonitor_->start();
    emit sessionCloseFailed();
    if (!guard) { return; }
    completionMonitor_->stop();
    busy_ = false;
    emit busyChanged(false);
    if (guard) { emit messageReady(message); }
  }, Qt::QueuedConnection);
}
void SessionBridge::cancel() { if (current_ && !closingSession_) { current_->cancel(); } }

namespace {
engine::LiveQueueLimits optionQueues(const core::Document& document) {
  engine::LiveQueueLimits result;
  for (const auto& [key, slot] : {std::pair{"max downloads", &result.maximumDownloads},
                                std::pair{"max active torrents", &result.maximumActive}}) {
    if (const auto* value = document.find(key)) {
      if (value->type != core::Node::Type::Integer || value->integer < 0 || value->integer > 1000000) {
        throw Failure{{storage::ErrorCode::InvalidFormat, std::string("Invalid setting: ") + key}};
      }
      *slot = static_cast<int>(value->integer);
    }
  }
  return result;
}
ui::OptionsPresentation optionPacket(const storage::ApplicationSettingsSnapshot& snapshot,
    quint64 session, quint64 request, quint64 revision, bool restart) {
  auto values = std::make_shared<ui::OptionsValues>();
  if(snapshot.source){const auto* originalUi=snapshot.source->find("ui");values->originalClassicUi=originalUi&&originalUi->type==core::Node::Type::Bytes&&originalUi->bytes=="az2";}
  values->fields.reserve(snapshot.fields.size());
  for (const auto& field : snapshot.fields) {
    ui::OptionsFieldState state;
    switch (field.state) {
      case storage::SettingState::Defaulted: state = ui::OptionsFieldState::Defaulted; break;
      case storage::SettingState::Stored: state = ui::OptionsFieldState::Stored; break;
      case storage::SettingState::InvalidType: state = ui::OptionsFieldState::InvalidType; break;
      case storage::SettingState::OutOfRange: state = ui::OptionsFieldState::OutOfRange; break;
      case storage::SettingState::UnavailableDefault: state = ui::OptionsFieldState::UnavailableDefault; break;
      default: throw Failure{{storage::ErrorCode::InvalidFormat, "Unknown configuration field state"}};
    }
    values->fields.push_back({field.id, state, field.value});
  }
  auto packet = std::make_shared<ui::OptionsPacket>();
  packet->session = session; packet->request = request; packet->revision = revision;
  packet->snapshot = std::move(values); packet->restartRequired = restart;
  return packet;
}
QString optionDefaultPath(const storage::ApplicationSettingsSnapshot& snapshot) {
  for (const auto& field : snapshot.fields) {
    if (field.id != core::SettingId::DefaultSavePath || !field.value
        || (field.state != storage::SettingState::Stored && field.state != storage::SettingState::Defaulted)) { continue; }
    const auto* raw = std::get_if<std::string>(&*field.value);
    if (!raw) { return {}; }
    const auto value = QString::fromUtf8(raw->data(), static_cast<qsizetype>(raw->size()));
    return value.toUtf8().toStdString() == *raw ? value : QString{};
  }
  return {};
}
}

#include "local_rating_bridge.inc"

bool SessionBridge::loadOptions(ui::OptionsRequest request) {
  if (profileClosed_ || !request.session || !request.request || request.session != publishedColumnLayoutSession_) { return false; }
  return dispatch([this, request](std::stop_token stop) {
    auto failure = std::make_shared<ui::OptionsPacket>();
    failure->session = request.session; failure->request = request.request;
    ui::OptionsPresentation result = failure;
    if (!host_ || host_->columnLayoutSession != request.session) {
      failure->failure = tr("This settings session is no longer active");
    } else {
      host_->optionsBaseline.reset();
      host_->optionsReadStop = stop;
      auto read = storage::readApplicationSettings(host_->lease, stop);
      if (!read) { failure->failure = text(read.error().message); }
      else if (stop.stop_requested()) { failure->failure = tr("Cancelled"); }
      else if (host_->optionsRevision == std::numeric_limits<quint64>::max()) {
        failure->failure = tr("Reopen the profile before editing more settings");
      } else {
        bool restart = !host_->engine;
        if (read->source) {
          const auto policy = engine::readNetworkPolicy(read->source.get(),
              {.mode = engine::NetworkMode::Normal, .socks4Capability = host_->networkPolicy.socks4Capability});
          if (policy) {
            auto live = engine::prepareLiveSettings(host_->networkPolicy, *policy);
            restart = restart || !live || live->restartFields != 0;
          }
        }
        result = optionPacket(*read, request.session, request.request, ++host_->optionsRevision, restart);
        host_->optionsBaseline = std::move(*read);
      }
    }
    QMetaObject::invokeMethod(this, [this, result = std::move(result)] {
      QPointer<SessionBridge> guard(this);
      if (result->session == publishedColumnLayoutSession_) { emit optionsReady(result); }
      if (!guard) { return; }
      completionMonitor_->stop(); busy_ = false; emit busyChanged(false);
    }, Qt::QueuedConnection);
  }, false);
}

bool SessionBridge::saveOptions(ui::OptionsSubmission submission) {
  if (profileClosed_ || !submission.session || !submission.request || !submission.revision
      || submission.session != publishedColumnLayoutSession_
      || submission.changes.size() > core::applicationSettingsCatalog().size()) { return false; }
  for (const auto& change : submission.changes) {
    const auto* field = core::applicationSetting(change.id);
    if (!field || !core::validApplicationSettingValue(*field, change.value)) { return false; }
  }
  return dispatch([this, submission = std::move(submission)](std::stop_token stop) {
    auto packet = std::make_shared<ui::OptionsSavePacket>();
    packet->session = submission.session; packet->request = submission.request; packet->revision = submission.revision;
    if (!host_ || host_->fileRecoveryRequired || host_->columnLayoutSession != submission.session || !host_->optionsBaseline
        || host_->optionsRevision != submission.revision || host_->optionsReadStop.stop_requested()
        || host_->optionsRevision == std::numeric_limits<quint64>::max()) {
      packet->failure = tr("Reload settings before saving; this snapshot is no longer current");
    } else {
      auto prepared = storage::prepareApplicationSettings(*host_->optionsBaseline, submission.changes);
      if (!prepared) { packet->failure = text(prepared.error().message); }
      else {
        auto policy = engine::readNetworkPolicy(&prepared->document(),
            {.mode = engine::NetworkMode::Normal, .socks4Capability = host_->networkPolicy.socks4Capability});
        if (!policy) { packet->failure = tr("Unsupported or invalid setting: %1").arg(text(policy.error().field)); }
        else {
          const auto queuePolicy = core::readQueuePolicy(&prepared->document());
          if (!queuePolicy) { packet->failure = tr("Invalid queue setting: %1").arg(text(queuePolicy.error().key)); }
          else {
          const auto queues = optionQueues(prepared->document());
          auto planned = engine::prepareLiveSettings(host_->networkPolicy, *policy, queues);
          if (!planned) { packet->failure = text(planned.error().message); }
          else {
            auto saved = storage::saveApplicationSettings(host_->lease, *host_->optionsBaseline, *prepared, stop);
            // Once committed, never replace the actual write result with cancellation.
            if (!saved) { packet->failure = text(saved.error().message); }
            else {
              packet->saved = true;
              packet->restartRequired = !host_->engine || planned->restartFields != 0;
              host_->optionsBaseline = std::move(*saved);
              ++host_->optionsRevision;
              if (host_->engine) {
                auto applied = host_->engine->applyLiveSettings(*policy, queues);
                if (applied) {
                  host_->networkPolicy = std::move(planned->effectivePolicy);
                  const auto queued = host_->engine->configureQueuePolicy(*queuePolicy,configuredSourceSmoothing(&prepared->document()).windowSeconds);
                  if (!queued) { applied = std::unexpected(queued.error()); }

                }
                else {
                  packet->restartRequired = true;
                  packet->failure = tr("Settings were saved; restart BiglyBTC to apply them");
                }
              }
              packet->snapshot = optionPacket(*host_->optionsBaseline, submission.session,
                  submission.request, host_->optionsRevision, packet->restartRequired);
              host_->intakeDefault = optionDefaultPath(*host_->optionsBaseline);
              if(host_->scheduler){
                const auto refreshed=host_->scheduler->reload(host_->lease);
                if(!refreshed){host_->schedulerFailure=text(refreshed.error().message);}
                else{host_->schedulerMemberships.clear();host_->schedulerTagsLoaded=false;host_->schedulerTagsAvailable=false;host_->schedulerLastTick.reset();}
              }
            }
          }
          }
        }
      }
    }
    const auto directory = host_ ? host_->intakeDefault : QString{};
    ui::OptionsSavePresentation result = std::move(packet);
    QMetaObject::invokeMethod(this, [this, result = std::move(result), directory] {
      QPointer<SessionBridge> guard(this);
      if (result->session == publishedColumnLayoutSession_) {
        emit optionsSaved(result);
        if (!guard) { return; }
        if (result->saved) { emit intakeDefaultsReady(result->session, directory); }
      }
      if (!guard) { return; }
      completionMonitor_->stop(); busy_ = false; emit busyChanged(false);
    }, Qt::QueuedConnection);
  }, false);
}

namespace {
ui::SchedulerPresentation schedulerPacket(const storage::SchedulerSnapshot& source,
    quint64 session, quint64 request) {
  auto values=std::make_shared<ui::SchedulerValues>();
  values->preferences={source.settings.enabled,source.settings.useTags,source.settings.overrideForced,source.settings.pollMilliseconds};
  if(source.configuration){values->configuration=*source.configuration;values->hasSavedSchedules=true;}
  values->inheritedOwnershipUnverified=source.inheritedOwnershipUnverified;
  values->interruptedAction=source.interruptedAction;
  values->directory=text(source.directoryLeaf);values->diagnostic=text(source.diagnostic);
  if(source.interrupted){
    const auto kind=source.interrupted->kind;
    const auto action=kind==storage::scheduler::ActionKind::Pause?QStringLiteral("Pause")
        :kind==storage::scheduler::ActionKind::Resume?QStringLiteral("Resume")
        :kind==storage::scheduler::ActionKind::Restart?QStringLiteral("Start"):QStringLiteral("Stop reason");
    values->interrupted=ui::SchedulerInterruptedAction{text(source.interrupted->hash.hex()),action,source.interrupted->imported,text(source.interrupted->identity)};
  }
  auto result=std::make_shared<ui::SchedulerPacket>();result->session=session;result->request=request;
  result->revision=source.revision;result->snapshot=std::move(values);return result;
}
}
void SessionBridge::ensureScheduler() {
  if(host_&&host_->fileRecoveryRequired){host_->schedulerFailure=tr("Finish file-operation recovery before opening schedules");return;}
  if(!host_||host_->schedulerAttempted){return;}host_->schedulerAttempted=true;
  if(!schedulerFactory_){host_->schedulerFailure=tr("Speed Scheduler is unavailable in this build");return;}
  try{host_->scheduler=schedulerFactory_(host_->lease);}
  catch(const std::exception& error){host_->schedulerFailure=tr("Speed Scheduler could not load: %1").arg(QString::fromUtf8(error.what()));}
  catch(...){host_->schedulerFailure=tr("Speed Scheduler could not load");}
  if(!host_->scheduler&&host_->schedulerFailure.isEmpty()){host_->schedulerFailure=tr("Speed Scheduler could not load");}
}
bool SessionBridge::loadScheduler(ui::SchedulerRequest request) {
  if(profileClosed_||!request.session||!request.request||request.session!=publishedColumnLayoutSession_){return false;}
  return dispatch([this,request](std::stop_token stop){
    auto failure=std::make_shared<ui::SchedulerPacket>();failure->session=request.session;failure->request=request.request;
    ui::SchedulerPresentation result=failure;
    if(!host_||host_->columnLayoutSession!=request.session){failure->failure=tr("This scheduling session is no longer active");}
    else{
      ensureScheduler();
      if(!host_->scheduler){failure->failure=host_->schedulerFailure;}
      else{
        auto read=host_->scheduler->reload(host_->lease,stop);
        if(!read){failure->failure=text(read.error().message);}
        else{host_->schedulerMemberships.clear();host_->schedulerTagsLoaded=false;result=schedulerPacket(host_->scheduler->snapshot(),request.session,request.request);}
      }
    }
    QMetaObject::invokeMethod(this,[this,result]{
      QPointer<SessionBridge> guard(this);if(result->session==publishedColumnLayoutSession_){emit schedulerReady(result);}
      if(!guard){return;}completionMonitor_->stop();busy_=false;emit busyChanged(false);
    },Qt::QueuedConnection);
  },false);
}
bool SessionBridge::saveScheduler(ui::SchedulerSubmission value) {
  if(profileClosed_||!value.session||!value.request||!value.revision||value.session!=publishedColumnLayoutSession_
      ||value.schedules.size()>storage::scheduler::MaximumSchedules){return false;}
  return dispatch([this,value=std::move(value)](std::stop_token stop){
    auto result=std::make_shared<ui::SchedulerSavePacket>();result->session=value.session;result->request=value.request;result->revision=value.revision;
    if(!host_||host_->fileRecoveryRequired||host_->columnLayoutSession!=value.session||!host_->scheduler){result->failure=tr("Reload schedules before saving");}
    else try{
      const auto& current=host_->scheduler->snapshot();
      if(current.revision!=value.revision){throw Failure{{storage::ErrorCode::Conflict,"Schedules changed; reload before saving"}};}
      constexpr int RateMax=std::numeric_limits<int>::max()/1024;
      if(value.preferences.pollMilliseconds<1000||value.defaultUpload<0||value.defaultUpload>RateMax
          ||value.defaultDownload<0||value.defaultDownload>RateMax){throw Failure{{storage::ErrorCode::InvalidFormat,"Invalid schedule speeds or interval"}};}
      storage::SchedulerXmlChanges xml;
      const storage::scheduler::Configuration empty;
      const auto& source=current.configuration?*current.configuration:empty;
      if(value.defaultUpload!=source.defaultUpload||value.defaultDownload!=source.defaultDownload||value.enforceDefaults!=source.defaultsEnforce){
        xml.defaults=storage::SchedulerXmlChanges::Defaults{value.defaultUpload,value.defaultDownload,value.enforceDefaults};
      }
      std::set<std::size_t> retained;
      for(const auto& row:value.schedules){
        if(row.sourceIndex){
          if(*row.sourceIndex>=source.schedules.size()||!retained.insert(*row.sourceIndex).second){throw Failure{{storage::ErrorCode::Conflict,"Invalid schedule selection"}};}
          if(row.value!=source.schedules[*row.sourceIndex]){xml.update.push_back({*row.sourceIndex,row.value});}
        }else{xml.append.push_back(row.value);}
      }
      for(std::size_t i=0;i<source.schedules.size();++i){if(!retained.contains(i)){xml.remove.push_back(i);}}
      const bool writeXml=xml.defaults||!xml.update.empty()||!xml.remove.empty()||!xml.append.empty();
      storage::SchedulerSettingsChanges settings;
      if(value.preferences.enabled!=current.settings.enabled){settings.enabled=value.preferences.enabled;}
      if(value.preferences.useTags!=current.settings.useTags){settings.useTags=value.preferences.useTags;}
      if(value.preferences.overrideForced!=current.settings.overrideForced){settings.overrideForced=value.preferences.overrideForced;}
      if(value.preferences.pollMilliseconds!=current.settings.pollMilliseconds){settings.pollMilliseconds=value.preferences.pollMilliseconds;}
      // Both edits are validated before the first write. There is no cross-file
      // atomicity claim: schedules commit first; settings failure is explicit.
      if(value.settleInterruptedHash){
        if(writeXml||settings.enabled||settings.useTags||settings.overrideForced||settings.pollMilliseconds
            ||!current.interrupted||text(current.interrupted->hash.hex())!=*value.settleInterruptedHash
            ||text(current.interrupted->identity)!=value.settleInterruptedIdentity){
          throw Failure{{storage::ErrorCode::Conflict,"Review the current interrupted action separately from schedule edits"}};
        }
        const auto hash=current.interrupted->hash;
        auto& manual=host_->manualRevisions[hash.hex()];
        if(manual==std::numeric_limits<std::uint64_t>::max()){throw Failure{{storage::ErrorCode::Conflict,"Manual action revision exhausted"}};}
        ++manual;host_->schedulerPaused.erase(hash.hex());
        static_cast<void>(take(host_->scheduler->settleInterrupted(host_->lease,value.revision,hash,value.settleInterruptedIdentity.toStdString(),stop)));
        result->settlement=ui::SchedulerSettlementResult{*value.settleInterruptedHash,true,{}};
        result->saved=true;host_->schedulerLastTick.reset();
        // Explicit settlement does not run a policy tick or change a torrent.
        // Future ordinary ticks retain their existing admission/manual fences.
      }else{
      if(writeXml){static_cast<void>(take(storage::patchSchedulerXml(current.originalXml,xml)));}
      if(writeXml){static_cast<void>(take(host_->scheduler->saveSchedules(host_->lease,current.revision,xml,stop)));result->schedulesSaved=true;}
      static_cast<void>(take(host_->scheduler->saveSettings(host_->lease,host_->scheduler->snapshot().revision,settings,
          result->schedulesSaved?std::stop_token{}:stop)));
      result->saved=true;host_->schedulerLastTick.reset();host_->schedulerRestoreDefaults=true;host_->schedulerMemberships.clear();host_->schedulerTagsLoaded=false;
      // Live policy application is independent of the confirmed save result.
      // Its diagnostics are retained; a failed tick never rolls back a save.
      tickScheduler();
      }
      // A successful rate publication reloads the scheduler's configuration CAS
      // and advances its editor revision. Return the final snapshot, not a stale
      // packet constructed before that tick.
      result->snapshot=schedulerPacket(host_->scheduler->snapshot(),value.session,value.request);
    }catch(const Failure& error){
      result->failure=text(error.error.message);
      if(value.settleInterruptedHash){
        result->settlement=ui::SchedulerSettlementResult{*value.settleInterruptedHash,false,result->failure};
        result->snapshot=schedulerPacket(host_->scheduler->snapshot(),value.session,value.request);
      }
      if(result->schedulesSaved){result->failure=tr("Schedules saved; settings were not saved: %1").arg(result->failure);
        result->snapshot=schedulerPacket(host_->scheduler->snapshot(),value.session,value.request);}
    }
    ui::SchedulerSavePresentation packet=result;
    QMetaObject::invokeMethod(this,[this,packet]{
      QPointer<SessionBridge> guard(this);if(packet->session==publishedColumnLayoutSession_){emit schedulerSaved(packet);}
      if(!guard){return;}completionMonitor_->stop();busy_=false;emit busyChanged(false);
    },Qt::QueuedConnection);
  },false);
}
void SessionBridge::tickScheduler() noexcept {
  if(!host_||!host_->engine){return;}
  try{
    if(host_->fileRecoveryRequired||storage::hasPendingOriginalCompaction(host_->lease)){return;}
    ensureScheduler();if(!host_->scheduler){return;}
    if(!host_->scheduler->snapshot().settings.enabled&&host_->schedulerPaused.empty()&&!host_->schedulerRestoreDefaults
        &&host_->scheduler->snapshot().ownershipBytes.empty()){return;}
    const auto now=std::chrono::steady_clock::now();
    const auto interval=std::chrono::milliseconds(host_->scheduler->snapshot().settings.pollMilliseconds);
    if(host_->schedulerLastTick&&now-*host_->schedulerLastTick<interval){return;}host_->schedulerLastTick=now;
    host_->schedulerFailure.clear();
    auto torrents=host_->schedulerTorrents();
    const auto actual=host_->engine->liveSettings();
    if(!actual||actual->rateLimits.upload.state==core::RateLimitState::Unavailable
        ||actual->rateLimits.download.state==core::RateLimitState::Unavailable){return;}
    const auto local=QDateTime::currentDateTime();
    auto plan=take(host_->scheduler->evaluate({local.date().dayOfWeek()%7,local.time().hour()*60+local.time().minute()},
        torrents,static_cast<int>(actual->rateLimits.upload.bytesPerSecond/1024),static_cast<int>(actual->rateLimits.download.bytesPerSecond/1024)));
    bool ratesApplied=true,configurationPublished=false;
    if(plan.uploadKiB||plan.downloadKiB){
      auto source=take(storage::readApplicationSettings(host_->lease));
      bool seedingOnly=false;
      if(host_->networkPolicy.seedingOnlyUpload){
        const auto live=host_->engine->managedDownloads();bool any=false,onlySeeds=true;
        for(const auto& row:live){if(row.retained&&row.phase!=engine::ManagedPhase::Stopping){any=true;onlySeeds=onlySeeds&&row.selectedComplete;}}
        seedingOnly=any&&onlySeeds&&host_->engine->pendingMagnets().empty();
      }
      std::vector<core::SettingChange> edits;
      if(plan.uploadKiB){edits.push_back({seedingOnly?core::SettingId::SeedingUploadLimit:core::SettingId::UploadLimit,std::int64_t{*plan.uploadKiB}});}
      if(plan.downloadKiB){edits.push_back({core::SettingId::DownloadLimit,std::int64_t{*plan.downloadKiB}});}
      auto prepared=take(storage::prepareApplicationSettings(source,edits));
      auto requested=engine::readNetworkPolicy(&prepared.document(),
          {.mode=engine::NetworkMode::Normal,.socks4Capability=host_->networkPolicy.socks4Capability});
      if(!requested){throw Failure{{storage::ErrorCode::InvalidFormat,"Invalid saved routing while applying scheduled rates"}};}
      auto safe=take(engine::prepareLiveSettings(host_->networkPolicy,*requested));
      static_cast<void>(take(storage::saveApplicationSettings(host_->lease,source,prepared)));
      configurationPublished=true;
      const auto applied=host_->engine->applyLiveSettings(*requested);
      ratesApplied=applied.has_value();if(applied){host_->networkPolicy=std::move(safe.effectivePolicy);}
      if(!applied){host_->schedulerFailure=tr("Scheduled speeds were saved; restart to apply them");}
    }
    static_cast<void>(take(host_->scheduler->acknowledgeRates(plan.id,ratesApplied,ratesApplied)));
    if(ratesApplied){host_->schedulerRestoreDefaults=false;}
    if(configurationPublished){
      // Our own confirmed config write invalidated the old action CAS baseline.
      // A reload retains same-byte, acknowledged current-session ownership.
      static_cast<void>(take(host_->scheduler->reload(host_->lease)));
      host_->schedulerMemberships.clear();host_->schedulerTagsLoaded=false;host_->schedulerTagsAvailable=false;
      const auto currentLimits=host_->engine->liveSettings();
      if(!currentLimits||currentLimits->rateLimits.upload.state==core::RateLimitState::Unavailable
          ||currentLimits->rateLimits.download.state==core::RateLimitState::Unavailable){
        throw Failure{{storage::ErrorCode::Io,"Actual limits unavailable after scheduled rate publication"}};
      }
      torrents=host_->schedulerTorrents();const auto currentTime=QDateTime::currentDateTime();
      plan=take(host_->scheduler->evaluate({currentTime.date().dayOfWeek()%7,currentTime.time().hour()*60+currentTime.time().minute()},
          torrents,static_cast<int>(currentLimits->rateLimits.upload.bytesPerSecond/1024),
          static_cast<int>(currentLimits->rateLimits.download.bytesPerSecond/1024)));
      // Only the first plan's rate effects were applied. New outstanding rate
      // changes remain unacknowledged for the next tick; actions use this fresh
      // plan and its exact new revision/current rows.
      static_cast<void>(take(host_->scheduler->acknowledgeRates(plan.id,false,false)));
    }
    for(const auto& action:plan.actions){
      auto current=host_->schedulerTorrents();
      const auto row=std::ranges::find(current,action.hash,&storage::SchedulerTorrent::hash);
      if(row==current.end()){continue;}
      auto began=host_->scheduler->beginAction(host_->lease,action,*row);
      if(!began){host_->schedulerFailure=text(began.error().message);continue;}
      bool intentConfirmed=false;
      try{
        if(action.kind!=storage::scheduler::ActionKind::SetStopReason){
          const bool pause=action.kind==storage::scheduler::ActionKind::Pause;
          auto persisted=storage::applyLibraryCommand(host_->lease,action.hash,pause?core::DownloadAction::Pause:core::DownloadAction::Resume);
          // A failed publication can be indeterminate; retain the journal and
          // revoke resume authority rather than claiming no filesystem effect.
          if(!persisted){throw Failure{persisted.error()};}intentConfirmed=true;
          auto controlled=host_->engine->pauseManaged(action.hash,pause);
          if(!controlled){throw Failure{controlled.error()};}
          if(pause){host_->schedulerPaused.insert(action.hash.hex());}else{host_->schedulerPaused.erase(action.hash.hex());}
        }
        static_cast<void>(take(host_->scheduler->finishAction(host_->lease,action,storage::SchedulerActionResult::Succeeded)));
      }catch(const Failure& error){
        static_cast<void>(host_->scheduler->finishAction(host_->lease,action,storage::SchedulerActionResult::Indeterminate));
        host_->schedulerFailure=text(error.error.message);
        static_cast<void>(intentConfirmed);break;
      }
    }
    // Saved Java permission is evaluated separately from acknowledged native
    // pause ownership; saved-only targets do not acquire fake LT tokens.
    const auto importedTime=QDateTime::currentDateTime();
    auto importedTargets=host_->schedulerImportedTargets();
    const auto imported=take(host_->scheduler->evaluateImported(host_->lease,
        {importedTime.date().dayOfWeek()%7,importedTime.time().hour()*60+importedTime.time().minute()},importedTargets));
    if(imported.unavailableTargets){host_->schedulerFailure=tr("Some saved Scheduler targets have unavailable completion observations");}
    for(const auto& action:imported.actions){
      auto current=host_->schedulerImportedTargets();
      const auto row=std::ranges::find(current,action.target.hash,&storage::ImportedSchedulerTarget::hash);
      if(row==current.end()){continue;}
      const auto began=host_->scheduler->beginImportedAction(host_->lease,action,*row);
      if(!began){host_->schedulerFailure=text(began.error().message);continue;}
      try{
        std::optional<storage::LibraryCommandResult> publication;
        if(action.kind!=storage::scheduler::ActionKind::SetStopReason){
          const auto command=action.kind==storage::scheduler::ActionKind::Resume?core::DownloadAction::Resume:core::DownloadAction::Start;
          publication=take(storage::applyLibraryCommand(host_->lease,action.target.hash,command));
          const auto& saved=*publication;
          if(!saved.pauseAfter&&saved.after.startup!=core::StartupState::Stopped){
            const auto active=host_->engine->managedDownloads();
            const auto live=std::ranges::find(active,action.target.hash,&engine::ManagedDownloadStatus::hash);
            const auto controlled=live!=active.end()&&live->retained
                ?host_->engine->queueManaged(action.target.hash,saved.after.forceStart,saved.after.startup==core::StartupState::Queued)
                :host_->engine->startManaged(host_->lease,{action.target.hash,host_->peerPort,saved.after.forceStart,saved.after.startup==core::StartupState::Queued});
            if(!controlled){throw Failure{controlled.error()};}
          }
          host_->schedulerPaused.erase(action.target.hash.hex());
        }
        if(action.kind==storage::scheduler::ActionKind::SetStopReason&&action.target.paused){host_->schedulerPaused.insert(action.target.hash.hex());}
        static_cast<void>(take(host_->scheduler->finishImportedAction(host_->lease,action,storage::SchedulerActionResult::Succeeded,
            publication?&*publication:nullptr)));
      }catch(const Failure& error){
        static_cast<void>(host_->scheduler->finishImportedAction(host_->lease,action,storage::SchedulerActionResult::Indeterminate));
        host_->schedulerFailure=text(error.error.message);break;
      }catch(...){
        static_cast<void>(host_->scheduler->finishImportedAction(host_->lease,action,storage::SchedulerActionResult::Indeterminate));
        throw;
      }
    }
    if(host_->schedulerFailure.isEmpty()){host_->schedulerFailure=text(host_->scheduler->snapshot().diagnostic);}
  }catch(const Failure& error){host_->schedulerFailure=text(error.error.message);}
  catch(const std::exception& error){host_->schedulerFailure=QString::fromUtf8(error.what());}
  catch(...){host_->schedulerFailure=tr("Scheduler policy is unavailable");}
  if(host_->schedulerFailure!=host_->schedulerPublishedFailure){
    host_->schedulerPublishedFailure=host_->schedulerFailure;
    if(!host_->schedulerFailure.isEmpty()){
      const auto message=host_->schedulerFailure;const auto session=host_->columnLayoutSession;
      QMetaObject::invokeMethod(this,[this,message,session]{if(!profileClosed_&&session==publishedColumnLayoutSession_){emit messageReady(message);}},Qt::QueuedConnection);
    }
  }
}

#include "hybrid_alias_bridge.inc"
#include "file_operations_bridge.inc"
} // namespace biglybtc::app
