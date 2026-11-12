#include "runtime_console.hpp"
#include "network_identity_controller.hpp"
#include "../ui/library_window.hpp"
#ifdef BIGLYBTC_HOST_GENERAL_CHATS
#include "general_chat_controller.hpp"
#include "swarm_discovery_controller.hpp"
#endif
#include "subscription_acquisition.hpp"
#include "original_filename_policy.hpp"
#include "torrent_intake.hpp"
#include "durable_intake_receipt.hpp"
#include "download_history_store.hpp"
#include "verified_seeding.hpp"
#include "allocation_intent.hpp"
#include "file_operations.hpp"
#include "linear_to_compact.hpp"
#include "original_storage_defaults.hpp"
#include "torrent_creator.hpp"
#include <thread>
#include "magnet_intake.hpp"
#include "pending_metadata_source.hpp"
#include <unistd.h>
#include "file_policy_commands.hpp"
#include "library_commands.hpp"
#include "network_policy.hpp"
#include "session_bridge.hpp"
#include "rss_http_fetch.hpp"
#include "saved_metasearch_service.hpp"
#include "metasearch_metainfo_fetch.hpp"
#include "metasearch_magnet_admission.hpp"
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QFile>
#include <QDir>
#include "saved_subscription_service.hpp"
#include "saved_sharing_service.hpp"
#include "existing_subscription_target.hpp"
#include "embedded_tracker.hpp"
#include <QJsonDocument>
#include <QJsonObject>
#include "rss_sources_catalog.hpp"
#include "i2p_saved_settings.hpp"
#include "owned_mixed_i2p_service.hpp"
#if BIGLYBTC_HOST_I2P_OWNER
#include "../services/i2p_provider/profile_bootstrap.hpp"
#include "../services/i2p_provider/owner_runtime_guard.hpp"
#include "plugin_settings.hpp"
#endif
#include "mixed_i2p_source.hpp"
#include "coexist_coordinator.hpp"
#include "application_settings.hpp"
#include "scheduler_service.hpp"
#include "ownership_file.hpp"
#include "torrent_state.hpp"
#include "state_cache_materialization.hpp"
#include "../adapters/local_rating.hpp"
#include <QDateTime>
#include "../engine/live_settings.hpp"
#include "owned_azureus_service.hpp"
#include "owned_dht_accounting.hpp"
#include "live_speed_mapper_binding.hpp"

#include "library_view.hpp"
#include "column_layout_view.hpp"
#include "biglybtc/saved_dnd.hpp"
#include "biglybtc/original_file_links.hpp"
#include "local_session.hpp"
#include "persistent_session.hpp"
#include "shared_seed_announce.hpp"
#include "overall_statistics_service.hpp"
#include "mainline_identity.hpp"
#include "source_rate_projection.hpp"
#include "minute_history_writer.hpp"
#include "saved_service_view.hpp"
#include "statistics_view.hpp"
#include "saved_download_tags_view.hpp"
#include "manual_tag_store.hpp"
#include "category_store.hpp"
#include "manual_tag_policy.hpp"
#include "biglybtc/tag_constraints.hpp"
#include "tag_constraint_worker.hpp"
#include "country_provider.hpp"
#include "../adapters/saved_speed_manager_view.hpp"
#include "../adapters/history_view.hpp"
#include "../adapters/history_week_view.hpp"
#include "../adapters/history_month_view.hpp"
#include "../adapters/history_range_view.hpp"

#include <QMetaObject>
#include <QPointer>
#include <QTimer>
#include <QUrl>
#include <QScopeGuard>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <limits>
#include <bit>
#include <chrono>
#include <algorithm>
#include <cmath>
#include <array>
#include <QStringList>
#include <map>
#include <deque>
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
    case engine::ManagedPhase::Allocating: return SessionBridge::tr("Allocating");
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
QString managedState(const engine::ManagedDownloadStatus& value){
  if(value.phase==engine::ManagedPhase::Checking&&!value.checkingProgressPpm){return SessionBridge::tr("Preparing");}
  if(value.phase==engine::ManagedPhase::Checking&&value.checkingProgressPpm){return SessionBridge::tr("Checking %1%").arg(static_cast<double>(*value.checkingProgressPpm)/10000.0,0,'f',1);}
  auto result=managedState(value.phase);
  if(value.phase==engine::ManagedPhase::Seeding&&value.firstPriority){result=SessionBridge::tr("Seeding — first priority");}
  if(value.forceStart&&(value.phase==engine::ManagedPhase::Seeding||value.phase==engine::ManagedPhase::Downloading)){result=SessionBridge::tr("Forced %1").arg(result);}
  return result;
}
constexpr quint16 actionBit(core::DownloadAction value) {
  const auto ordinal = static_cast<unsigned int>(value);
  return ordinal < core::DownloadActionCount ? static_cast<quint16>(1U << ordinal) : quint16{0};
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
template<class T>T displaySum(T a,T b,bool& capped){
  if(b>std::numeric_limits<T>::max()-a){capped=true;return std::numeric_limits<T>::max();}return a+b;
}
template<class T>T payloadSum(T a,T b,bool& capped){
  if(a<0||b<0){return -1;}if(b>std::numeric_limits<T>::max()-a){capped=true;return -1;}return a+b;
}
void take(std::expected<void, storage::Error> value) {
  if (!value) { throw Failure{std::move(value.error())}; }
}
}
namespace {
template<class Store> bool categoryDeletionSupported(const Store& value){
  if constexpr(requires{value.deletionSupported();}){return value.deletionSupported();}else{return false;}
}
template<class Store> std::expected<storage::CategoryCommit,storage::Error> categoryRemove(
    Store& value,const storage::CategorySnapshot& expected,std::string_view name,std::stop_token stop){
  if constexpr(requires{value.remove(expected,name,stop);}){return value.remove(expected,name,stop);}
  else{return std::unexpected(storage::Error{storage::ErrorCode::Unsupported,"Category deletion requires the journal-enabled backend"});}
}
}
struct SessionBridge::Host {
#ifdef BIGLYBTC_HOST_GENERAL_CHATS
  std::shared_ptr<GeneralChats> chats;
#endif
  ~Host(){
#ifdef BIGLYBTC_HOST_GENERAL_CHATS
    if(chats){chats->requestStop();chats->join();chats.reset();}
#endif
if(recheckWork&&recheckWork->request.receiver){recheckWork->request.receiver->request_stop();}if(automaticReceiver){automaticReceiver->request_stop();}automaticCandidates.clear();automaticCandidateKeys.clear();if(subscriptionAcquisition){subscriptionAcquisition->cancel();subscriptionAcquisition.reset();}closeRssFetch();closeMetasearch();closePublications();if(subscriptions){subscriptions->cancel();subscriptions.reset();}if(engine){engine->clearMagnetCompletionHook();}if(constraintWorker){constraintWorker->close();}mixed.reset();i2p.reset();} // Immediate no-throw fallback joins before the held lease.
  struct Known { QString name; std::optional<qint64> bytes{}; std::optional<bool> assumedComplete{}; QString trackerName{}; };
  std::filesystem::path profile;
  unsigned short peerPort;
  bool normalNetwork = false;
  std::optional<I2pServiceOptions> i2pOptions;
  // Startup-only filesystem work. A failed/cancelled attempt cannot be
  // retried by later Start/enable commands in this retained Host lifetime.
  // Configuration/runtime failures apply to both identities; a preserved
  // partial publication applies only to its independently selected index.
  std::optional<storage::Error> i2pBootstrapFailure;
  std::optional<storage::Error> i2pBootstrapPureFailure,i2pBootstrapMixedFailure;
  const std::optional<storage::Error>& bootstrapFailure(bool mixedIndex)const noexcept{
    return i2pBootstrapFailure?i2pBootstrapFailure:mixedIndex?i2pBootstrapMixedFailure:i2pBootstrapPureFailure;
  }
  QString bootstrapReview()const{
    QString result;
    const auto append=[&](const char* label,const std::optional<storage::Error>& error){
      if(error){if(!result.isEmpty()){result+=QStringLiteral("; ");}result+=QString::fromLatin1(label)+text(error->message).left(384);}
    };
    append("I2P: ",i2pBootstrapFailure);append("Pure I2P: ",i2pBootstrapPureFailure);append("Mixed I2P: ",i2pBootstrapMixedFailure);
    return result.left(1024);
  }
  std::unique_ptr<OwnedI2pService> i2p;
  std::unique_ptr<OwnedMixedI2pService> mixed;
  struct MixedStatus {bool active=false;std::uint64_t generation=0,routeNonce=0;std::string failure;};
  std::map<std::string,MixedStatus> mixedStatus;
  std::set<std::string> mixedBindingTargets;
  bool mixedJoined=true;
  bool mixedCapable()const noexcept{return coexistence()&&engine::PersistentSession::mixedNetworksAvailable();}
  std::map<std::string,ui::I2pPacket> i2pPackets;
  std::map<std::string,OwnedI2pService::Observation> i2pFacts;
  ui::I2pPacket i2pPacket;
  quint64 i2pSequence=0;
  enum class Route { Public, I2p, Mixed, Unsupported };
  using PrimaryStamp=std::array<std::uint64_t,7>;
  struct PrimaryPresentation {
    std::optional<PrimaryStamp> stamp{};
    std::optional<qint64> bytes{};
    core::SavedDndFacts dnd{};
    std::optional<QString> displayName{};
    QString trackerName{};
    QStringList trackerPreferences{};
    std::array<QVariant,18> originalValues{};
    Route route=Route::Unsupported;
    bool publicOnly=false;
    bool simple=false;
    std::optional<core::Document> links{};
  };
  std::map<std::string,PrimaryPresentation> primaryPresentations; // Read-only UI facts, never Start authority.

  bool coexistence()const noexcept{return i2pOptions&&i2pOptions->publicCoexistence;}
  bool privateOnly()const noexcept{return i2pOptions&&!i2pOptions->publicCoexistence;}
  Route route(const core::TorrentHash& hash)const {
    auto store=storage::TorrentStateStore::open(lease);if(!store){return Route::Unsupported;}
    auto state=store->loadAttributes(lease,hash);if(!state){return Route::Unsupported;}
    const auto* a=state->document().find("attributes");const auto* n=a?a->find("networks"):nullptr;
    return classifyRoute(n);
  }
  Route classifyRoute(const core::Node* n)const {
    if(!n){return Route::Public;}
    if(n->type!=core::Node::Type::List){return Route::Unsupported;}
    // Existing rows already grant Public. Keep saved overlay selections while
    // the explicitly selected Public-only application mode leaves them inactive.
    if(normalNetwork&&!i2pOptions){
      bool publicSelected=false;std::set<std::string> names;
      for(const auto& item:n->children){
        if(item.type!=core::Node::Type::Bytes
            ||(item.bytes!="Public"&&item.bytes!="I2P"&&item.bytes!="Tor")
            ||!names.insert(std::string(item.bytes)).second){return Route::Unsupported;}
        publicSelected|=item.bytes=="Public";
      }
      if(publicSelected){return Route::Public;}
    }
    if(n->children.size()==2&&n->children[0].type==core::Node::Type::Bytes&&n->children[1].type==core::Node::Type::Bytes
        &&((n->children[0].bytes=="Public"&&n->children[1].bytes=="I2P")||(n->children[0].bytes=="I2P"&&n->children[1].bytes=="Public"))){return Route::Mixed;}
    if(n->children.size()!=1||n->children[0].type!=core::Node::Type::Bytes){return Route::Unsupported;}
    if(n->children[0].bytes=="I2P"){return Route::I2p;}
    return n->children[0].bytes=="Public"?Route::Public:Route::Unsupported;
  }
  bool pureI2p(const core::TorrentHash& hash)const{return route(hash)==Route::I2p;}
  std::expected<void,storage::Error> bootstrapNormalI2p(std::stop_token stop){
    if(!normalNetwork||!i2pOptions){return {};}
#if BIGLYBTC_HOST_I2P_OWNER
    // This is readonly loaded-image qualification, BEFORE any FS gate. Mere
    // bootstrap files never replace the factory's own current runtime/source
    // checks or cause a Qt owner, session, transport, provider or Actor to start.
    const auto pin=services::i2p::provider::privateOwnerRuntimePin();
    if(!OwnedI2pService::available()||!pin){return std::unexpected(storage::Error{storage::ErrorCode::Unsupported,"Approved I2P runtime unavailable; identity files were not created"});}
    const bool mixedRuntime=coexistence()&&OwnedMixedI2pService::available()&&engine::PersistentSession::mixedNetworksAvailable();
    auto access=lease.sourceAccess();if(!access){return std::unexpected(access.error());}
    auto config=storage::ConfigFile::load(lease,"biglybt.config");if(!config){return std::unexpected(config.error());}
    if(config->recoverySource()!=storage::RecoverySource::Primary){return std::unexpected(storage::Error{storage::ErrorCode::Conflict,"Review original I2P configuration recovery before identity bootstrap"});}
    auto binding=storage::originalPluginBinding("azneti2phelper","azneti2phelper");if(!binding){return std::unexpected(storage::Error{storage::ErrorCode::InvalidFormat,"Invalid original I2P plugin binding"});}
    storage::OriginalPluginSettings settings(config->document(),std::move(*binding));
    const bool startup=settings.loadedAtStartup(),enabled=settings.boolean("enable",true),pure=settings.boolean("azi2phelper.pure.enabled",true);
    const bool mixed=coexistence()&&settings.boolean("azi2phelper.mix.enabled",true);
    if(!settings.wrongTypes().empty()){return std::unexpected(storage::Error{storage::ErrorCode::InvalidFormat,"Malformed original I2P startup/index setting; identity files were not created"});}
    if(!startup||!enabled){return {};}
    const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(30);
    for(const auto key:{services::i2p::provider::BootstrapKey::Pure,services::i2p::provider::BootstrapKey::Mixed}){
      const bool selected=key==services::i2p::provider::BootstrapKey::Pure?pure:mixed&&mixedRuntime;
      if(!selected){continue;}
      auto result=services::i2p::provider::bootstrapI2pProfile(lease,{.mode=services::i2p::provider::BootstrapMode::Normal,.key=key},stop,end);
      if(!result){
        (key==services::i2p::provider::BootstrapKey::Pure?i2pBootstrapPureFailure:i2pBootstrapMixedFailure)=result.error();
        // Cancellation/deadline exhaustion aborts open. An index-specific
        // refusal must not skip the independently selected other identity.
        if(result.error().code==storage::ErrorCode::Cancelled||stop.stop_requested()){return std::unexpected(result.error());}
      }
    }
    return {}; // Source access is released BEFORE every actual owner factory.
#else
    // Preserve stock refusal stubs and their existing read-only profile view.
    static_cast<void>(stop);return {};
#endif
  }
  std::expected<void,storage::Error> publishTarget(const core::TorrentHash& hash,const std::function<std::expected<void,storage::Error>()>& f){
    const auto acquisition=storage::requireNoPendingSubscriptionAcquisition(lease);if(!acquisition){return std::unexpected(acquisition.error());}
    if(recoveryDrainStarted||(storage::hasPendingCategoryDeletion(lease)||storage::hasPendingFileOperation(lease))){return std::unexpected(storage::Error{storage::ErrorCode::Conflict,"Guarded inventory/state writes require explicit profile recovery or review"});}
    const auto inner=[&]{return mixed?mixed->publish(hash,f):f();};
    auto result=i2p?i2p->publish(hash,inner):inner();
    if(!result&&((i2p&&!i2p->sourceCurrent())||(mixed&&!mixed->sourceCurrent()))){if(i2p){i2p->revoke();}if(mixed){mixed->revoke();}}return result;
  }
  // Serialized Host-worker observation. Disk state cannot certify that an
  // engine, pending admission or anonymous controller has released a target.
  std::expected<void,storage::Error> subscriptionTargetAvailable(const core::TorrentHash& hash) const {
    const auto refused=[] {return std::unexpected(storage::Error{storage::ErrorCode::Conflict,
        "The subscription target is still controlled by a live or pending operation"});};
    if(i2p||mixed||fileRecoveryRequired||recoveryDrainStarted){return refused();}
    const auto key=hash.hex();
    if(recheckTargets.contains(key)||pendingIntents.contains(key)||pendingTargets.contains(key)||privateImports.contains(key)
        ||allocationPending.contains(key)||verifiedSeedPending.contains(key)){return refused();}
    if(engine){
      // managedAvailable also rejects legacy registrations and poisoned owners;
      // an empty status vector alone does not establish absence of an owner.
      if(!engine->managedAvailable()){return refused();}
      for(const auto& row:engine->managedDownloads()){if(row.hash==hash){return refused();}}
      for(const auto& row:engine->pendingMagnets()){if(row.hash==hash){return refused();}}
    }
    return {};
  }
  std::expected<void,storage::Error> publishPositions(const std::function<std::expected<void,storage::Error>()>& f){
    const auto acquisition=storage::requireNoPendingSubscriptionAcquisition(lease);if(!acquisition){return std::unexpected(acquisition.error());}
    if(recoveryDrainStarted||(storage::hasPendingCategoryDeletion(lease)||storage::hasPendingFileOperation(lease))){return std::unexpected(storage::Error{storage::ErrorCode::Conflict,"Guarded inventory/state writes require explicit profile recovery or review"});}
    const auto inner=[&]{return mixed?mixed->publishPositions(f):f();};
    auto result=i2p?i2p->publishPositions(inner):inner();
    if(!result&&((i2p&&!i2p->sourceCurrent())||(mixed&&!mixed->sourceCurrent()))){if(i2p){i2p->revoke();}if(mixed){mixed->revoke();}}return result;
  }
  void rememberMixedFailure(const core::TorrentHash& hash,const storage::Error& error){
    auto& value=mixedStatus[hash.hex()];value.active=false;value.failure=error.message;
  }
  std::expected<void,storage::Error> prepareMixed(const core::TorrentHash& hash){
    if(const auto& failure=bootstrapFailure(true);failure){return std::unexpected(*failure);}
    if(!mixedCapable()||!engine){return std::unexpected(storage::Error{storage::ErrorCode::Unsupported,"The mixed I2P provider is unavailable"});}
    if(!mixed){
      auto transport=engine->mixedTransport();if(!transport){return std::unexpected(transport.error());}
      // The facade reads mixed defaults and captures its key/config under one
      // same-profile gate; explicitly supplied development options still win.
      auto created=OwnedMixedI2pService::create(lease,*i2pOptions,*transport);if(!created){return std::unexpected(created.error());}
      mixed=std::move(*created);mixedJoined=false;
    }
    auto result=mixed->prepare(hash);if(result){mixedStatus[hash.hex()]={};}return result;
  }
  std::expected<void,storage::Error> removeMixed(const core::TorrentHash& hash){
    mixedBindingTargets.erase(hash.hex());auto result=mixed?mixed->remove(hash):std::expected<void,storage::Error>{};
    mixedStatus.erase(hash.hex());return result;
  }
  void attachMixed(const core::TorrentHash& hash){
    if(!mixed){return;}auto source=engine->mixedSource(hash);
    if(!source){rememberMixedFailure(hash,source.error());return;}
    const auto result=mixed->attach(*source);if(!result){rememberMixedFailure(hash,result.error());}
  }
  void retryMixed(const core::TorrentHash& hash){
    const auto removed=removeMixed(hash);if(!removed){rememberMixedFailure(hash,removed.error());return;}
    auto prepared=prepareMixed(hash);if(!prepared){rememberMixedFailure(hash,prepared.error());return;}
    auto source=engine->mixedSource(hash);if(!source){rememberMixedFailure(hash,source.error());return;}
    const auto bound=publishTarget(hash,[&]{return mixed->bind(*source);});
    if(!bound){rememberMixedFailure(hash,bound.error());return;}attachMixed(hash);
  }
  void pollMixed(){
    if(!mixed){return;}auto observations=mixed->observations();
    bool unavailable=false;std::vector<core::TorrentHash> changed;
    for(const auto& row:observations){
      if(row.metadataOnly){
        if(row.sourceChanged){
          // Filesystem/key loss revokes the actual metadata handle as well as
          // I2P effects. Keep its original recovery intent for an explicit retry.
          if(engine){
            auto withdrawn=engine->withdrawPreparedMagnet(row.hash,storage::Error{storage::ErrorCode::Conflict,"Mixed metadata source changed; recovery intent retained"});
            if(!withdrawn){admissionFailures.insert_or_assign(row.hash.hex(),withdrawn.error());}
          }
          unavailable=true;
          admissionFailures.insert_or_assign(row.hash.hex(),storage::Error{storage::ErrorCode::Conflict,"Mixed metadata source changed; inspect the original profile before retrying"});
          continue;
        }
        if(row.active&&engine&&mixedMetadata.contains(row.hash.hex())&&!activatedMetadata.contains(row.hash.hex())){
          auto activated=engine->activatePreparedMagnet(row.hash);if(activated){activatedMetadata.insert(row.hash.hex());}else{admissionFailures.insert_or_assign(row.hash.hex(),activated.error());}
        }
        if(row.unavailable){admissionFailures.insert_or_assign(row.hash.hex(),storage::Error{storage::ErrorCode::Conflict,row.failure.empty()?"I2P metadata route unavailable":row.failure});}
        continue;
      }
      auto& value=mixedStatus[row.hash.hex()];
      value={row.active,row.generation,row.routeNonce,row.failure};
      unavailable|=row.unavailable;if(row.sourceChanged){changed.push_back(row.hash);}
    }
    if(unavailable||!changed.empty()){
      static_cast<void>(mixed->close());
      const auto final=mixed->observations();mixedJoined=std::ranges::all_of(final,[](const auto& row){return row.joined;});
      mixed.reset();mixedBindingTargets.clear();
      for(auto& [hash,value]:mixedStatus){(void)hash;value.active=false;if(value.failure.empty()){value.failure="I2P unavailable. Use Start to retry; Public remains selected.";}}
      for(const auto& hash:changed){
        const auto stopped=engine->stopManaged(hash);
        admissionFailures.insert_or_assign(hash.hex(),storage::Error{storage::ErrorCode::Conflict,"Mixed torrent source changed. The admission was withdrawn; inspect the original profile before retrying."});
        if(!stopped){admissionFailures.insert_or_assign(hash.hex(),stopped.error());}
      }
    }
  }
  std::expected<void,storage::Error> stopMixed(){
    if(!mixed){mixedStatus.clear();return {};}auto result=mixed->close();
    const auto final=mixed->observations();mixedJoined=std::ranges::all_of(final,[](const auto& row){return row.joined;});mixed.reset();
    mixedStatus.clear();mixedBindingTargets.clear();return result;
  }
  std::expected<storage::LibraryCommandResult,storage::Error> libraryCommand(const core::TorrentHash& hash,core::DownloadAction action,std::span<const storage::LibraryCompletionPool> pools={}){
    std::optional<storage::LibraryCommandResult> result;
    const auto write=[&]()->std::expected<void,storage::Error>{auto v=storage::applyLibraryCommand(lease,hash,action,pools);if(!v){return std::unexpected(v.error());}result=std::move(*v);return {};};
    const bool moving=action==core::DownloadAction::MoveTop||action==core::DownloadAction::MoveUp||action==core::DownloadAction::MoveDown||action==core::DownloadAction::MoveBottom;
    const auto done=moving?publishPositions(write):publishTarget(hash,write);
    if(!done){return std::unexpected(done.error());}return std::move(*result);
  }
  std::set<std::string> reportedResumeDecisions;
  std::expected<void,storage::Error> startPublic(engine::ManagedDownloadOptions options){
    const auto acquisition=storage::requireNoPendingSubscriptionAcquisition(lease);if(!acquisition){return std::unexpected(acquisition.error());}
    if(recoveryDrainStarted||(storage::hasPendingCategoryDeletion(lease)||storage::hasPendingFileOperation(lease))){return std::unexpected(storage::Error{storage::ErrorCode::Conflict,"Guarded inventory/state writes require explicit profile recovery or review"});}
    const auto selected=route(options.hash);const bool both=selected==Route::Mixed&&mixedCapable();
    if(!engine||privateOnly()||(selected!=Route::Public&&!both)){return std::unexpected(storage::Error{storage::ErrorCode::Unsupported,"Saved torrent networks require a provider that is not available; the selection was preserved"});}
    // Retired completion sources must be consumed before prepare mutates
    // their checkpoint postimages. Optional history failure never blocks Start.
    const auto historyWarning=consumeHistoryCompletions({},true);
    if(!historyWarning.isEmpty()){downloadHistoryIntakeNotice=historyWarning;}
    const auto hash=options.hash;
    if(both){options.mixedNetworks=true;const auto prepared=prepareMixed(hash);
      if(prepared){mixedBindingTargets.insert(hash.hex());}else{rememberMixedFailure(hash,prepared.error());}
    }
    const auto started=engine->startManaged(lease,std::move(options),i2p?i2p->retainedPayloads():engine::RetainedPayloads{});
    mixedBindingTargets.erase(hash.hex());
    if(both){if(started){attachMixed(hash);pollMixed();}else{static_cast<void>(removeMixed(hash));}}
    return started;
  }
  std::expected<void,storage::Error> startSaved(const core::TorrentHash& hash,bool force=false,bool queued=false){
    const auto acquisition=storage::requireNoPendingSubscriptionAcquisition(lease);if(!acquisition){return std::unexpected(acquisition.error());}
    return route(hash)==Route::I2p?startI2p(hash):startPublic({hash,peerPort,force,queued});
  }
  CoexistCoordinator coexistQueue;
  void scheduleCoexistence(){if(coexistence()&&engine){take(coexistQueue.tick(*engine,i2p.get(),lease,networkPolicy,
      [this](const auto& operation){return publishPositions(operation);}));}}
  void requireI2pIdle(bool categoryRecovery=false) {
    if(!categoryRecovery){requireCategoryInventoryWritable();}
    if(!mixedStatus.empty()){throw Failure{{storage::ErrorCode::Conflict,"Stop the mixed torrents before changing profile settings or files"}};}
    if(mixed){take(stopMixed());}
    if(i2p&&!i2pPackets.empty()){throw Failure{{storage::ErrorCode::Conflict,"Stop the I2P torrents before changing profile settings or files"}};}
    if(i2p){take(stopI2p());}
  }
  bool i2pJoined=true;
  static QString i2pState(const OwnedI2pService::Observation& v){
    if(!v.failure.empty()){return SessionBridge::tr("Error — I2P");}
    switch(v.phase){
      case OwnedI2pService::Phase::Prepared:case OwnedI2pService::Phase::Starting:return SessionBridge::tr("Connecting — I2P");
      case OwnedI2pService::Phase::Active:return !v.checked?SessionBridge::tr("Checking — I2P"):v.queued?SessionBridge::tr("Queued — I2P"):v.seeding?SessionBridge::tr("Seeding — I2P"):SessionBridge::tr("Downloading — I2P");
      case OwnedI2pService::Phase::Retiring:return SessionBridge::tr("Stopping — I2P");
      case OwnedI2pService::Phase::Withdrawn:return SessionBridge::tr("Error — I2P");
    }return {};
  }
  std::expected<void,storage::Error> stopI2p(){
    if(!i2p){return {};}auto stopped=i2p->close();i2pJoined=i2p->observation().joined;coexistQueue.privateOwnerChanged();i2p.reset();i2pPackets.clear();i2pFacts.clear();
    i2pPacket.state=SessionBridge::tr("Stopped — I2P");i2pPacket.downloadRate=0;i2pPacket.uploadRate=0;i2pPacket.peers=0;i2pPacket.torrents.clear();
    if(!stopped){i2pPacket.failure=text(stopped.error().message);return std::unexpected(stopped.error());}return {};
  }
  std::expected<void,storage::Error> startI2p(const core::TorrentHash& hash){
    const auto acquisition=storage::requireNoPendingSubscriptionAcquisition(lease);if(!acquisition){return std::unexpected(acquisition.error());}
    if(recoveryDrainStarted||(storage::hasPendingCategoryDeletion(lease)||storage::hasPendingFileOperation(lease))){return std::unexpected(storage::Error{storage::ErrorCode::Conflict,"Guarded inventory/state writes require explicit profile recovery or review"});}
    if(const auto& failure=bootstrapFailure(false);failure){return std::unexpected(*failure);}
    if(!i2pOptions||!pureI2p(hash)){return std::unexpected(storage::Error{storage::ErrorCode::Unsupported,"This private preview requires an explicitly saved I2P-only torrent"});}
    if(i2p){auto added=i2p->add(hash,engine?engine->retainedPayloads():engine::RetainedPayloads{});pollI2p();return added;}
    const auto config=storage::ConfigFile::load(lease,"biglybt.config");if(!config){return std::unexpected(config.error());}
    auto options=readSavedI2pOptions(config->document(),*i2pOptions);if(!options){return std::unexpected(options.error());}
    options->externalQueue=coexistence();auto started=OwnedI2pService::start(lease,hash,std::move(*options),engine?engine->retainedPayloads():engine::RetainedPayloads{});if(!started){return std::unexpected(started.error());}
    i2p=std::move(*started);coexistQueue.privateOwnerChanged();i2pJoined=false;pollI2p();return {};
  }
  void pollI2p(){
    if(!i2p){return;}const auto rows=i2p->observations();i2pPackets.clear();i2pFacts.clear();ui::I2pPacket total;bool capped=false;total.payloadDownloaded=0;total.payloadUploaded=0;
    bool withdrawn=rows.empty()&&i2p->observation().phase==OwnedI2pService::Phase::Withdrawn;
    for(const auto& row:rows){const auto& v=row.value;i2pFacts.emplace(row.hash.hex(),v);ui::I2pPacket p;p.hash=text(row.hash.hex());p.state=i2pState(v);p.failure=text(v.failure);
      p.verifiedBytes=v.verifiedBytes;p.payloadDownloaded=v.payloadDownloaded;p.payloadUploaded=v.payloadUploaded;p.downloadRate=v.downloadRate;p.uploadRate=v.uploadRate;p.peers=v.peers;
      p.trackerAnnounces=v.trackerAnnounces;p.trackerReplies=v.trackerReplies;p.trackerErrors=v.trackerErrors;
      total.verifiedBytes=displaySum(total.verifiedBytes,p.verifiedBytes,capped);total.payloadDownloaded=payloadSum(total.payloadDownloaded,p.payloadDownloaded,capped);total.payloadUploaded=payloadSum(total.payloadUploaded,p.payloadUploaded,capped);
      total.downloadRate=displaySum(total.downloadRate,p.downloadRate,capped);total.uploadRate=displaySum(total.uploadRate,p.uploadRate,capped);total.peers=displaySum(total.peers,p.peers,capped);total.trackerAnnounces=displaySum(total.trackerAnnounces,p.trackerAnnounces,capped);total.trackerReplies=displaySum(total.trackerReplies,p.trackerReplies,capped);total.trackerErrors=displaySum(total.trackerErrors,p.trackerErrors,capped);
      total.torrents.push_back({p.hash,p.state,p.failure,p.verifiedBytes,p.payloadDownloaded,p.payloadUploaded,p.downloadRate,p.uploadRate,p.peers,v.queued,v.admission,v.generation,v.downloadLimit,v.uploadLimit,v.queueUnavailable,v.completionPositionUnavailable});
      total.globalDownloadLimit=v.globalDownloadLimit;total.globalUploadLimit=v.globalUploadLimit;total.loopbackPeerClasses=v.loopbackPeerClasses;
      i2pPackets.emplace(row.hash.hex(),std::move(p));withdrawn|=v.phase==OwnedI2pService::Phase::Withdrawn;
    }
    total.state=rows.empty()?SessionBridge::tr("I2P session idle"):rows.size()==1?i2pPackets.begin()->second.state:SessionBridge::tr("Shared I2P session — %1 torrents").arg(rows.size());
    if(rows.size()==1){total.hash=i2pPackets.begin()->second.hash;total.failure=i2pPackets.begin()->second.failure;}
    if(capped){total.failure=SessionBridge::tr("Some aggregate values exceed the display range; individual measurements remain available.");}
    i2pPacket=std::move(total);
    if(withdrawn){
      for(auto& [hash,p]:i2pPackets){if(p.failure.isEmpty()){p.failure=SessionBridge::tr("I2P identity, source or router connection withdrew. Use Start to retry.");}admissionFailures.insert_or_assign(hash,storage::Error{storage::ErrorCode::Conflict,p.failure.toStdString()});}
      static_cast<void>(i2p->close());i2pJoined=i2p->observation().joined;coexistQueue.privateOwnerChanged();i2p.reset();i2pPacket.state=SessionBridge::tr("Error — I2P");
    }
  }
  void i2pCommands(const ui::DownloadTargets& targets,core::DownloadAction action,std::stop_token stop){
    requireCategoryInventoryWritable();
    if(fileRecoveryRequired||storage::hasPendingFileOperation(lease)||storage::hasPendingOriginalCompaction(lease)||storage::hasPendingTorrentIntake(lease)){throw Failure{{storage::ErrorCode::Conflict,"Finish pending profile recovery before changing I2P downloads"}};}
    if(targets.empty()){throw Failure{{storage::ErrorCode::Unsupported,"Select an I2P torrent"}};}
    std::vector<core::TorrentHash> hashes;std::set<std::string> seen;
    for(const auto& target:targets){checkpoint(stop);accepts(target);const auto hash=identity(target);
      const auto row=std::ranges::find_if(lastSnapshot->items,[&](const auto& value){return value.hash==target.hash;});
      if(row==lastSnapshot->items.end()||!(row->actionMask&actionBit(action))||!pureI2p(hash)||!seen.insert(hash.hex()).second){throw Failure{{storage::ErrorCode::Conflict,"Refresh the I2P selection before applying this action"}};}hashes.push_back(hash);
    }
    for(const auto& hash:hashes){
      if(action==core::DownloadAction::MoveTop||action==core::DownloadAction::MoveUp||action==core::DownloadAction::MoveDown||action==core::DownloadAction::MoveBottom){
        if(i2p){take(i2p->move(hash,action,movePools));}else{take(storage::applyLibraryCommand(lease,hash,action,movePools));}
      }else if(i2p){take(i2p->command(hash,action,engine?engine->retainedPayloads():engine::RetainedPayloads{}));}
      else{const auto changed=take(storage::applyLibraryCommand(lease,hash,action,{}));
        if(!changed.pauseAfter&&changed.after.startup!=core::StartupState::Stopped&&(action==core::DownloadAction::Start||action==core::DownloadAction::Resume||action==core::DownloadAction::Queue||action==core::DownloadAction::ForceStart)){take(startI2p(hash));}}
      admissionFailures.erase(hash.hex());
    }pollI2p();
  }

  services::network_identity::Input networkIdentityConfig;
  engine::NetworkPolicy networkPolicy; // Member defaults keep this isolated until configure succeeds.
  std::string normalMainlineDirectory="mldht";
  std::vector<core::TorrentHash> startupQueue;
  std::size_t startupNext=0,startupDone=0,startupTotal=0;
  bool startupCachePrepared=false;
  std::chrono::steady_clock::time_point nextAzureusTargets{};
  std::set<std::string> startupWithdrawn;
  bool advanceStartup(std::stop_token stop,std::function<void(std::size_t,std::size_t)> cacheProgress) {
    if(!normalNetwork||i2pOptions||!engine||startupNext>=startupQueue.size()){return false;}
    if(!startupCachePrepared){
      const auto prepared=storage::materializeStateCache(lease,stop,std::move(cacheProgress));
      if(!prepared&&prepared.error().code==storage::ErrorCode::Cancelled){throw Failure{prepared.error()};}
      startupCachePrepared=true;
      if(prepared){engine::detail::runtimeConsole("library.state-cache")<<" examined="<<prepared->examined<<" hydrated="<<prepared->hydrated<<" state_writes="<<prepared->stateWrites<<" cache_writes="<<prepared->cacheWrites<<" retained="<<prepared->retained<<'\n'<<std::flush;}
      else{engine::detail::runtimeConsole("library.state-cache")<<" fallback="<<engine::detail::consoleText(prepared.error().message)<<'\n'<<std::flush;}
    }
    // Bound one worker turn; a large restored library must leave commands and
    // close responsive. Re-read saved intent once for this batch, not per row.
    const auto inventory=take(storage::ConfigFile::load(lease,"downloads.config"));
    const auto source=core::readLibrary(inventory.document());if(!source){throw Failure{{storage::ErrorCode::InvalidFormat,"Cannot read startup inventory"}};}
    std::map<std::string,const core::SavedDownload*> saved;
    std::set<std::string> duplicates,paused;
    for(const auto& item:source->downloads){if(item.hash){const auto key=item.hash->hex();
      if(!saved.emplace(key,&item).second){duplicates.insert(key);}}}
    for(const auto& item:source->paused){paused.insert(item.hash.hex());}
    std::set<std::string> retained;
    for(const auto& item:engine->managedDownloads()){if(item.retained){retained.insert(item.hash.hex());}}
    const auto started=std::chrono::steady_clock::now();
    std::size_t attempted=0;bool changed=false;
    while(startupNext<startupQueue.size()&&attempted<32
        &&std::chrono::steady_clock::now()-started<std::chrono::milliseconds{250}){
      checkpoint(stop);const auto hash=startupQueue[startupNext++];const auto key=hash.hex();++attempted;
      const auto current=saved.find(key);
      if(current==saved.end()||duplicates.contains(key)||startupWithdrawn.contains(key)
          ||paused.contains(key)||retained.contains(key)||!current->second->persistent
          ||current->second->startup==core::StartupState::Stopped){continue;}
      const auto& value=*current->second;
      const auto admitted=startSaved(hash,value.forceStart,value.startup==core::StartupState::Queued);
      if(admitted){admissionFailures.erase(key);}else{admissionFailures.insert_or_assign(key,admitted.error());}
      changed=true;
    }
    startupDone=startupNext;
    if(startupNext==startupQueue.size()){startupQueue.clear();startupNext=0;startupWithdrawn.clear();}
    return changed;
  }
  engine::NetworkPolicyOptions normalNetworkOptions(const core::Document* config=nullptr)const {
    engine::NetworkPolicyOptions options;options.mode=engine::NetworkMode::Normal;
    options.socks4Capability=networkPolicy.socks4Capability;
    options.mldhtDirectory=normalMainlineDirectory;
    const auto proxyEnabled=[&](std::string_view key){const auto* node=config?config->find(key):nullptr;
      return node&&(node->type!=core::Node::Type::Integer||node->integer!=0);};
    // Normal profile ownership explicitly permits its configured router mapping.
    // Proxy routes retain their existing no-direct-mapping boundary.
    options.portMapping=normalNetwork&&!proxyEnabled("Enable.Proxy")&&!proxyEnabled("Proxy.Data.Enable");
    if(normalNetwork&&!i2pOptions){options.context=engine::NetworkPolicyContext::ExistingLibraryPublic;}
    return options;
  }
  std::optional<std::pair<storage::ErrorCode,std::string>> reportedMainlineFailure;
  QString mainlineNotice() {
    if(!engine){return {};}const auto observed=engine->mainlineDiscovery();
    if(!observed.originalIdentityRequested||!observed.failure){return {};}
    const auto stamp=std::pair{observed.failure->code,observed.failure->message};
    if(reportedMainlineFailure==stamp){return {};}reportedMainlineFailure=stamp;
    return SessionBridge::tr("Mainline discovery is unavailable: %1").arg(text(observed.failure->message));
  }
  bool socks4CapabilityChecked = false; // Worker-owned, independent of config repair success.
  using MagnetStamp = std::tuple<std::uint64_t, engine::MagnetPhase, bool, std::string,
      std::optional<storage::ErrorCode>, std::string,std::uint64_t>;
  std::map<std::string, MagnetStamp> lastMagnetPhases;
  std::map<std::string, engine::PendingMagnet> pendingIntents;
  std::set<std::string> pendingTargets;
  std::optional<storage::Error> pendingReadFailure;
  bool pendingSourceCurrent = false;
  std::map<std::string, std::uint64_t> reportedMagnetAdmissions;
  using ManagedStamp = std::tuple<engine::ManagedPhase, std::optional<core::QueueReason>,
      bool, std::uint32_t, bool, std::int64_t, bool, int, std::string>;
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
  void rememberKnown(const std::string& key,QString name,std::optional<qint64> bytes){auto& value=known[key];value.name=std::move(name);value.bytes=bytes;}
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
  std::unique_ptr<storage::ManualTagStore> manualTagStore;
  storage::ManualTagSnapshot manualTagSnapshot;
  std::shared_ptr<const core::SavedDownloadTags> manualTagEditorSource;
  quint64 lastManualTagRequest=0;
#include "tag_constraint_host.inc"
#include "download_history_intake.inc"
  #include "download_history_completion.inc"
#include "download_history_view_host.inc"
#include "rss_sources_host.inc"
#include "subscriptions_host.inc"
#include "metasearch_host.inc"
#include "publication_host.inc"
#include "recheck_host.inc"
  void beginManualTagRead() {
    if(manualTagEnded||manualTagRevision==std::numeric_limits<quint64>::max()){
      throw Failure{{storage::ErrorCode::Unsupported,"Manual-tag presentation lifetime is exhausted"}};
    }
    manualTagsEnabled=true;++manualTagRevision;
    manualTagSnapshot={};manualTagEditorSource.reset();manualTagSource.reset();manualMembership.reset();
    manualTagReadState=ui::ManualTagReadState::Failed;manualTagFailure.clear();manualJoinFailure.clear();
  }
  void acceptManualTagSnapshot(storage::ManualTagSnapshot snapshot,
      const std::shared_ptr<storage::ManualTagSnapshot>& holder) {
    *holder=std::move(snapshot);manualTagSnapshot=*holder;
    manualTagEditorSource=std::shared_ptr<const core::SavedDownloadTags>(holder,holder->tags());
    manualTagSource=holder->missing()?nullptr:manualTagEditorSource;
    manualTagReadState=holder->missing()?ui::ManualTagReadState::Missing:ui::ManualTagReadState::Ready;
    manualTagFailure.clear();manualJoinFailure.clear();manualMembership.reset();
    schedulerTagsLoaded=false;schedulerTagsAvailable=false;
  }
  void reloadManualTags(std::stop_token stop) {
    auto holder=std::make_shared<storage::ManualTagSnapshot>();beginManualTagRead();
    // Only an explicit read can reopen uncertainty; the failed edit is never
    // replayed. Healthy unchanged reloads keep the actual session allocator.
    if(manualTagStore&&manualTagStore->recoveryRequired()){take(manualTagStore->close());manualTagStore.reset();}
    if(!manualTagStore){manualTagStore=take(storage::ManualTagStore::open(lease,stop));acceptManualTagSnapshot(take(manualTagStore->snapshot()),holder);}
    else{acceptManualTagSnapshot(take(manualTagStore->reload(stop)),holder);}
    constraintPaused=false;constraintReloadAt=std::chrono::steady_clock::now()+std::chrono::seconds(30);
  }
  void manualTagCommandState(ui::ManualTagCommandPacket& packet)const {
    packet.generation=manualTagRevision;packet.libraryRevision=revision;
    packet.readState=manualTagReadState;packet.writable=manualTagSnapshot.writable();
    packet.tags=manualTagEditorSource;
  }
#include "category_host.inc"
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
  std::map<std::string,core::TorrentAllocationMode> allocationPending;
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
    if(const auto* rows=file.document().find("downloads");rows&&rows->type==core::Node::Type::List){for(const auto& row:rows->children){
      if(row.find(storage::AllocationModeKey)||row.find(storage::AllocationStateKey)){const auto* id=row.find("torrent_hash");if(!id||id->type!=core::Node::Type::Bytes){throw Failure{{storage::ErrorCode::InvalidFormat,"Invalid allocation identity"}};}
        const auto hash=core::TorrentHash::parse(id->bytes);if(!hash){throw Failure{{storage::ErrorCode::InvalidFormat,"Invalid allocation identity"}};}
        const auto intent=take(storage::readAllocationIntent(lease,*hash));if(intent&&intent->state!=storage::AllocationState::Complete){allocationPending.insert_or_assign(hash->hex(),intent->mode);}
      }
    }}
    verifiedSeedIntentsLoaded=true;
  }
  bool verifiedSeedBlocked(const std::string& key,const std::vector<engine::ManagedDownloadStatus>& active) {
    if(recheckTargets.contains(key)){return true;}
    loadVerifiedSeedIntents();
    for(const auto& item:active){if(item.hash.hex()==key&&item.allocationMode!=core::TorrentAllocationMode::None){
      if(item.allocationComplete&&!item.failure){allocationPending.erase(key);}else{allocationPending.insert_or_assign(key,item.allocationMode);}
      if(item.retained||allocationPending.contains(key)){return true;}
    }}
    if(allocationPending.contains(key)){return true;}
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
    if(recheckWork){return false;}
    if (i2p || fileRecoveryRequired || storage::hasPendingFileOperation(lease)
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

  std::map<std::string,std::shared_ptr<const engine::PendingMetadataSource>> mixedMetadata;
  std::set<std::string> activatedMetadata;
  struct PrivateImport {
    engine::PendingMagnet intent;
    std::string bytes;
    bool committed=false,routeReleased=false,reservationReleased=false;
    std::chrono::steady_clock::time_point next{};
  };
  // Completion deletes the pending row. Retain the subsequent release work
  // independently, so a transient owner failure cannot orphan that admission.
  std::map<std::string,PrivateImport> privateImports;
  engine::MagnetNetwork magnetContext()const noexcept{
    return !i2pOptions?engine::MagnetNetwork::Public:privateOnly()?engine::MagnetNetwork::I2p:engine::MagnetNetwork::PublicAndI2p;
  }
  std::expected<void,storage::Error> publishPending(const core::TorrentHash& hash,const std::function<std::expected<void,storage::Error>()>& operation){
    if(recoveryDrainStarted||(storage::hasPendingCategoryDeletion(lease)||storage::hasPendingFileOperation(lease))){return std::unexpected(storage::Error{storage::ErrorCode::Conflict,"Guarded inventory/state writes require explicit profile recovery or review"});}
    const auto inner=[&]{return mixed?mixed->publishPendingMagnet(hash,operation):operation();};
    auto result=i2p?i2p->publishPendingMagnet(hash,inner):inner();
    if(!result&&((i2p&&!i2p->sourceCurrent())||(mixed&&!mixed->sourceCurrent()))){if(i2p){i2p->revoke();}if(mixed){mixed->revoke();}}
    return result;
  }
  std::vector<engine::PendingMagnetStatus> pendingMagnets()const{
    auto result=engine?engine->pendingMagnets():std::vector<engine::PendingMagnetStatus>{};
    if(i2p){auto pure=i2p->pendingMagnets();result.insert(result.end(),std::make_move_iterator(pure.begin()),std::make_move_iterator(pure.end()));}
    return result;
  }
  std::expected<void,storage::Error> startPending(const engine::PendingMagnet& intent){
    const auto acquisition=storage::requireNoPendingSubscriptionAcquisition(lease);if(!acquisition){return std::unexpected(acquisition.error());}
    if(recoveryDrainStarted||(storage::hasPendingCategoryDeletion(lease)||storage::hasPendingFileOperation(lease))){return std::unexpected(storage::Error{storage::ErrorCode::Conflict,"Guarded inventory/state writes require explicit profile recovery or review"});}
    const auto previous=pendingMagnets();const auto old=std::ranges::find(previous,intent.hash,&engine::PendingMagnetStatus::hash);
    if(old!=previous.end()&&old->retained){return std::unexpected(storage::Error{storage::ErrorCode::Conflict,"Metadata admission is still resolving or retiring"});}
    if(old!=previous.end()&&intent.network!=engine::MagnetNetwork::Public){
      // An explicit retry retires only the old, already removed reservation;
      // its exact original recovery row remains available for a fresh attempt.
      if(intent.network==engine::MagnetNetwork::I2p&&i2p){auto released=i2p->releaseMagnet(intent.hash);if(!released){return released;}}
      else if(mixedMetadata.contains(intent.hash.hex())){
        auto released=mixed?mixed->removeMetadata(intent.hash):std::expected<void,storage::Error>{};
        if(released&&engine){released=engine->releasePreparedMagnet(intent.hash);}if(!released){return released;}
        mixedMetadata.erase(intent.hash.hex());activatedMetadata.erase(intent.hash.hex());
      }
    }
    if(intent.network==engine::MagnetNetwork::Public){
      if(!engine||privateOnly()){return std::unexpected(storage::Error{storage::ErrorCode::Unsupported,"Public magnet owner unavailable"});}
      return engine->startMagnet(lease,intent,profile);
    }
    if(!i2pOptions){return std::unexpected(storage::Error{storage::ErrorCode::Unsupported,"I2P magnet provider unavailable"});}
    if(const auto& failure=bootstrapFailure(intent.network!=engine::MagnetNetwork::I2p);failure){return std::unexpected(*failure);}
    if(intent.network==engine::MagnetNetwork::I2p){
      if(i2p){return i2p->addMagnet(intent);}
      auto config=storage::ConfigFile::load(lease,"biglybt.config");if(!config){return std::unexpected(config.error());}
      auto options=readSavedI2pOptions(config->document(),*i2pOptions);if(!options){return std::unexpected(options.error());}
      options->externalQueue=coexistence();
      auto created=OwnedI2pService::startPending(lease,intent,std::move(*options));if(!created){return std::unexpected(created.error());}
      i2p=std::move(*created);i2pJoined=false;coexistQueue.privateOwnerChanged();return {};
    }
    if(!mixedCapable()||!engine){return std::unexpected(storage::Error{storage::ErrorCode::Unsupported,"Mixed magnet provider unavailable"});}
    if(!mixed){auto transport=engine->mixedTransport();if(!transport){return std::unexpected(transport.error());}
      auto created=OwnedMixedI2pService::create(lease,*i2pOptions,*transport);if(!created){return std::unexpected(created.error());}
      mixed=std::move(*created);mixedJoined=false;}
    auto prepared=mixed->prepareMagnet(intent);if(!prepared){return prepared;}
    auto sealed=engine->prepareMagnetMetadata(lease,intent,profile,[this,&intent](auto seal){return publishPending(intent.hash,[&]{return mixed->bindMetadata(std::move(seal));});});
    if(!sealed){(void)mixed->removeMetadata(intent.hash);return std::unexpected(sealed.error());}
    mixedMetadata.insert_or_assign(intent.hash.hex(),*sealed);
    auto attached=mixed->attachMetadata(*sealed);if(!attached){(void)engine->withdrawPreparedMagnet(intent.hash,attached.error());return attached;}return {};
  }
  std::expected<void,storage::Error> cancelPending(const engine::PendingMagnet& intent){
    const auto live=pendingMagnets();const auto found=std::ranges::find(live,intent.hash,&engine::PendingMagnetStatus::hash);
    if(found!=live.end()&&found->retained){
      return intent.network==engine::MagnetNetwork::I2p&&i2p?i2p->cancelMagnet(intent.hash):engine?engine->cancelMagnet(intent.hash):std::expected<void,storage::Error>{std::unexpected(storage::Error{storage::ErrorCode::Conflict,"Metadata owner unavailable"})};
    }
    auto cancelled=publishPending(intent.hash,[&]{return engine::cancelPendingMagnet(lease,intent);});
    if(cancelled){
      if(found!=live.end()&&intent.network==engine::MagnetNetwork::I2p&&i2p){cancelled=i2p->releaseMagnet(intent.hash);}
      else if(mixedMetadata.contains(intent.hash.hex())){cancelled=mixed?mixed->removeMetadata(intent.hash):std::expected<void,storage::Error>{};
        if(cancelled&&engine){cancelled=engine->releasePreparedMagnet(intent.hash);}if(cancelled){mixedMetadata.erase(intent.hash.hex());activatedMetadata.erase(intent.hash.hex());}}
    }return cancelled;
  }
  void pollPrivateMagnets(){
    // This Host worker imports only after actual metadata handle removal and
    // weak expiry. The Qt owner retains bytes until explicit successful release.
    const auto live=pendingMagnets();
    for(const auto& value:live){
      if(privateImports.contains(value.hash.hex())){continue;}
      auto found=pendingIntents.find(value.hash.hex());if(found==pendingIntents.end()){continue;}const auto intent=found->second;
      if(intent.network==engine::MagnetNetwork::Public||value.retained){continue;}
      if(value.phase==engine::MagnetPhase::Cancelled){
        const auto done=cancelPending(intent);if(!done){admissionFailures.insert_or_assign(value.hash.hex(),done.error());}continue;
      }
      if(value.phase!=engine::MagnetPhase::Retiring||value.failure){continue;}
      auto bytes=intent.network==engine::MagnetNetwork::I2p&&i2p?i2p->takeCompletedMetadata(value.hash):engine?engine->takeCompletedMagnetMetadata(value.hash):std::expected<std::string,storage::Error>{std::unexpected(storage::Error{storage::ErrorCode::Conflict,"Metadata owner unavailable"})};
      if(!bytes){admissionFailures.insert_or_assign(value.hash.hex(),bytes.error());continue;}
      // Allocate retry state before the durable import can delete its row.
      auto [at,inserted]=privateImports.emplace(value.hash.hex(),PrivateImport{intent,std::move(*bytes)});
      if(!inserted){continue;}
      const auto& metadata=at->second.bytes;
      const auto filename="BiglyBTC.metadata."+value.hash.hex()+".config";
      bool newlyImported=false;
      const auto completed=publishPending(value.hash,[&]()->std::expected<void,storage::Error>{
        auto existing=storage::ConfigFile::load(lease,filename);
        if(existing){if(existing->recoverySource()!=storage::RecoverySource::Primary||existing->document().serialize()!=metadata){return std::unexpected(storage::Error{storage::ErrorCode::Conflict,"Received metadata staging conflicts with an earlier result"});}}
        else{if(existing.error().code!=storage::ErrorCode::Missing){return std::unexpected(existing.error());}auto made=storage::ConfigFile::createMissing(lease,filename,metadata);if(!made){return std::unexpected(made.error());}}
        auto imported=engine::completePendingMagnet(lease,intent,profile/filename,networkPolicy,&newlyImported,
            normalNetwork?engine::MagnetCompletionHook{[this](storage::IntakeHistoryOutcome outcome){return consumeMagnetHistoryIntake(std::move(outcome));}}:engine::MagnetCompletionHook{});return imported?std::expected<void,storage::Error>{}:std::expected<void,storage::Error>{std::unexpected(imported.error())};
      });
      if(!completed){admissionFailures.insert_or_assign(value.hash.hex(),completed.error());privateImports.erase(at);continue;}
      at->second.committed=true;if(newlyImported){noteConstraintIntake(value.hash);}
    }
    for(auto at=privateImports.begin();at!=privateImports.end();){
      auto& work=at->second;const auto key=at->first;const auto hash=work.intent.hash;
      if(!work.committed){at=privateImports.erase(at);continue;}
      const auto now=std::chrono::steady_clock::now();if(now<work.next){++at;continue;}
      work.next=now+std::chrono::seconds(1);
      if(!work.routeReleased){
        auto released=work.intent.network==engine::MagnetNetwork::I2p&&i2p?i2p->releaseMagnet(hash):mixed?mixed->removeMetadata(hash):std::expected<void,storage::Error>{};
        if(!released){admissionFailures.insert_or_assign(key,released.error());++at;continue;}
        work.routeReleased=true;
      }
      if(!work.reservationReleased){
        auto released=mixedMetadata.contains(key)&&engine?engine->releasePreparedMagnet(hash):std::expected<void,storage::Error>{};
        if(!released){admissionFailures.insert_or_assign(key,released.error());++at;continue;}
        work.reservationReleased=true;
      }
      mixedMetadata.erase(key);activatedMetadata.erase(key);
      const auto filename="BiglyBTC.metadata."+key+".config";
      // Remove only our exact consumed sidecar, releasing the gate before SDK
      // payload admission. Ordinary saved-library Start handles later retries.
      {auto held=lease.sourceAccess();if(held){auto staged=storage::ConfigFile::load(lease,filename);if(staged&&staged->document().serialize()==work.bytes&&::unlinkat(lease.directoryHandle(),filename.c_str(),0)==0){(void)::fsync(lease.directoryHandle());}}}
      admissionFailures.erase(key);
      if(!work.intent.stopped){auto started=startSaved(hash,work.intent.forceStart);if(!started){admissionFailures.insert_or_assign(key,started.error());}else{admissionFailures.erase(key);}}
      pendingIntents.erase(key);at=privateImports.erase(at);
    }
  }

  void readMagnets() {
    auto read = engine::readPendingMagnets(lease, networkPolicy,magnetContext());
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
    if(!downloadHistoryIntakeNotice.isEmpty()){return downloadHistoryIntakeNotice;}
    return {};
  }
  quint16 magnetActions(const engine::PendingMagnet* intent, const engine::PendingMagnetStatus* live) const {
    if (!pendingSourceCurrent || !intent || (intent->network==engine::MagnetNetwork::Public?(!engine||!engine->managedAvailable()):(!i2pOptions||!OwnedI2pService::available()))) { return 0; }
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
    if (!pendingSourceCurrent || (!engine&&!i2pOptions)) { return; }
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
        std::expected<core::TorrentHash,storage::Error> completed=std::unexpected(storage::Error{storage::ErrorCode::Conflict,"Pending import publication failed"});
        auto published=publishPending(intent.hash,[&]()->std::expected<void,storage::Error>{completed=engine::completePendingMagnet(lease,intent,existing->saved.torrentFile,networkPolicy);return completed?std::expected<void,storage::Error>{}:std::expected<void,storage::Error>{std::unexpected(completed.error())};});
        if(!published){admitted=std::unexpected(published.error());}
        if (!completed) { admitted = std::unexpected(completed.error()); }
      } else if (!intent.destination) {
        admitted = std::unexpected(storage::Error{storage::ErrorCode::Missing, "Recovered magnet needs an explicit destination"});
      } else {
        admitted = startPending(intent);
      }
      if (!admitted) { admissionFailures.insert_or_assign(hash, admitted.error()); }
      else { admissionFailures.erase(hash); }
    }
    readMagnets();
  }
  ui::Presentation query(std::stop_token stop, const std::function<void(std::size_t,std::size_t)>& opening = {}) {
    loadVerifiedSeedIntents();
    checkpoint(stop);
    auto librarySource = take(storage::readLibrarySource(lease));
    auto& source = librarySource.view;
    // A read-only presentation borrows one fresh immutable cache snapshot.
    // Commands still use route()/loadAttributes and revalidate on actual Start.
    std::map<std::string,const core::Node*> cachedAttributes;
    if(librarySource.cacheDocument){
      if(const auto* entries=librarySource.cacheDocument->document().find("state");entries&&entries->type==core::Node::Type::List){
        for(const auto& entry:entries->children){
          const auto* hash=entry.find("hash");
          if(!hash||hash->type!=core::Node::Type::Bytes||(hash->bytes.size()!=20&&hash->bytes.size()!=32)){continue;}
          if(const auto id=core::TorrentHash::parse(hash->bytes)){cachedAttributes.insert_or_assign(id->hex(),&entry);}
        }
      }
    }
    QStringList preferredTrackerHosts;
    ui::TransferDisplaySettings displayUnits;
    bool inlineDetails=false,detailsOnDoubleClick=true,confirmExit=false;
    if(const auto settings=storage::ConfigFile::load(lease,"biglybt.config")){
      const auto boolean=[&](std::string_view key,bool fallback){const auto* value=settings->document().find(key);return value&&value->type==core::Node::Type::Integer?static_cast<std::uint32_t>(value->integer)!=0:fallback;};
      inlineDetails=boolean("Library.ShowTabsInTorrentView",false);
      confirmExit=boolean("confirmationOnExit",false);
      if(const auto* mode=settings->document().find("list.dm.dblclick");mode&&mode->type==core::Node::Type::Bytes&&!mode->bytes.empty()){detailsOnDoubleClick=mode->bytes=="1";}
      displayUnits.siLabels=boolean("config.style.useSIUnits",false);displayUnits.forceBinary=boolean("config.style.forceSIValues",true);
      displayUnits.rateBits=boolean("config.style.useUnitsRateBits",false);displayUnits.stopAtMegabytes=boolean("config.style.doNotUseGB",false);
      if(const auto* value=settings->document().find("mtv.trackername.pref.hosts");value&&value->type==core::Node::Type::Bytes){
        for(const auto& host:text(std::string(value->bytes)).split(';')){if(!host.trimmed().isEmpty()){preferredTrackerHosts.push_back(host.trimmed());}}
      }
    }
    const auto trackerDisplay=[&](const core::Node* primary,const core::Node* groups){
      const auto host=[](const core::Node* node){if(!node||node->type!=core::Node::Type::Bytes||node->bytes.size()>65536){return QString{};}const auto url=QUrl(text(std::string(node->bytes)),QUrl::StrictMode);return url.isValid()?url.host():QString{};};
      QString chosen=host(primary);
      if(!preferredTrackerHosts.isEmpty()&&!preferredTrackerHosts.contains(chosen)&&groups&&groups->type==core::Node::Type::List){
        bool found=false;for(const auto& tier:groups->children){if(tier.type!=core::Node::Type::List){continue;}for(const auto& url:tier.children){const auto candidate=host(&url);if(preferredTrackerHosts.contains(candidate)){chosen=candidate;found=true;break;}}if(found){break;}}
      }
      if(chosen.endsWith(QStringLiteral(".dht"))){return QStringLiteral("dht");}
      if(chosen.endsWith(QStringLiteral(".i2p"))||chosen.endsWith(QStringLiteral(".onion"))){return chosen;}
      const auto labels=chosen.split('.');QString result;int used=0;
      for(qsizetype i=labels.size();i>0;--i){if(used>4){break;}const auto& label=labels[i-1];if(used>=2&&label.size()>=11){break;}result=result.isEmpty()?label:label+'.'+result;++used;}
      return result;
    };
    std::optional<storage::TorrentStateStore> primaryStates;
    bool primaryOpenFailed=false;
    std::optional<storage::TorrentStateFile> primaryRow;
    std::string primaryRowKey;
    const auto primaryDocument=[&](const core::TorrentHash& hash)->const core::Document*{
      const auto key=hash.hex();if(primaryRow&&primaryRowKey==key){return &primaryRow->document();}
      primaryRow.reset();primaryRowKey=key;
      if(!primaryStates&&!primaryOpenFailed){auto store=storage::TorrentStateStore::open(lease);if(store){primaryStates.emplace(std::move(*store));}else{primaryOpenFailed=true;}}
      if(!primaryStates){return nullptr;}auto state=primaryStates->load(lease,hash);if(!state){return nullptr;}
      primaryRow.emplace(std::move(*state));return &primaryRow->document();
    };
    const auto metadataSize=[](const core::Document& doc)->std::optional<qint64>{
      const auto* info=doc.find("info");if(!info){return {};}
      if(const auto* length=info->find("length")){if(length->type==core::Node::Type::Integer&&length->integer>=0){return length->integer;}return {};}
      const auto* files=info->find("files");if(!files||files->type!=core::Node::Type::List){return {};}
      qint64 total=0;for(const auto& file:files->children){const auto* length=file.find("length");if(!length||length->type!=core::Node::Type::Integer||length->integer<0||length->integer>std::numeric_limits<qint64>::max()-total){return {};}total+=length->integer;}return total;
    };
    const auto originalColumns=[&](const core::Node* attrs){
      std::array<QVariant,18> values{};if(!attrs){return values;}
      const auto number=[&](std::string_view key)->QVariant{const auto* n=attrs->find(key);return n&&n->type==core::Node::Type::Integer?QVariant::fromValue(static_cast<qint64>(n->integer)):QVariant{};};
      const auto string=[&](std::string_view key)->QVariant{const auto* n=attrs->find(key);return n&&n->type==core::Node::Type::Bytes?QVariant{text(std::string(n->bytes))}:QVariant{};};
      values[5]=string("category");values[10]=number("timesincedl");values[11]=number("timesinceul");values[12]=number("mergedata");values[14]=string("comment");
      if(const auto* params=attrs->find("parameters")){if(const auto* added=params->find("stats.download.added.time");added&&added->type==core::Node::Type::Integer){values[6]=QVariant::fromValue(static_cast<qint64>(added->integer));}
        if(const auto* completed=params->find("stats.download.completed.time");completed&&completed->type==core::Node::Type::Integer&&completed->integer>0){values[7]=QVariant::fromValue(static_cast<qint64>(completed->integer));}
      }
      if(!values[7].isValid()){values[7]=values[6];}
      if(!values[12].isValid()){values[12]=qint64{0};}
      if(!values[10].isValid()){values[10]=qint64{-1};}if(!values[11].isValid()){values[11]=qint64{-1};}
      return values;
    };
    std::map<std::string,QStringList> displayTags;
    if(const auto tags=storage::readSavedDownloadTagsView(lease);tags&&tags->snapshot){
      for(const auto& tag:tags->snapshot->tags){if(!tag.rawName||tag.duplicateId){continue;}for(const auto& hash:tag.uniqueHashes){displayTags[hash.hex()].push_back(text(*tag.rawName));}}
      for(auto& [hash,names]:displayTags){(void)hash;names.sort(Qt::CaseInsensitive);}
    }
    const int primaryDirectory=::openat(lease.directoryHandle(),"active",O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW);
    const auto primaryDirectoryCleanup=qScopeGuard([primaryDirectory]{if(primaryDirectory>=0){::close(primaryDirectory);}});
    const auto primaryStamp=[&](const core::TorrentHash& hash)->std::optional<PrimaryStamp>{
      struct stat value{};const auto filename=hash.hex()+".dat";
      if(primaryDirectory<0||::fstatat(primaryDirectory,filename.c_str(),&value,AT_SYMLINK_NOFOLLOW)!=0||!S_ISREG(value.st_mode)||value.st_size<0){return {};}
      return PrimaryStamp{static_cast<std::uint64_t>(value.st_dev),static_cast<std::uint64_t>(value.st_ino),static_cast<std::uint64_t>(value.st_size),static_cast<std::uint64_t>(value.st_mtim.tv_sec),static_cast<std::uint64_t>(value.st_mtim.tv_nsec),static_cast<std::uint64_t>(value.st_ctim.tv_sec),static_cast<std::uint64_t>(value.st_ctim.tv_nsec)};
    };
    const auto primaryPresentation=[&](const core::TorrentHash& hash)->const PrimaryPresentation*{
      const auto key=hash.hex();const auto stamp=primaryStamp(hash);const bool publicOnly=normalNetwork&&!i2pOptions;
      if(const auto found=primaryPresentations.find(key);stamp&&found!=primaryPresentations.end()&&found->second.stamp==stamp&&found->second.trackerPreferences==preferredTrackerHosts&&found->second.publicOnly==publicOnly){return &found->second;}
      const auto* doc=primaryDocument(hash);if(!doc){primaryPresentations.erase(key);return nullptr;}
      const auto* attributes=doc->find("attributes");PrimaryPresentation value;value.bytes=metadataSize(*doc);value.dnd=core::readSavedDndFacts(attributes);value.route=classifyRoute(attributes?attributes->find("networks"):nullptr);value.trackerName=trackerDisplay(doc->find("announce"),doc->find("announce-list"));value.trackerPreferences=preferredTrackerHosts;value.publicOnly=publicOnly;
      value.originalValues=originalColumns(attributes);
      if(const auto* info=doc->find("info")){value.simple=info->find("length")!=nullptr&&info->find("files")==nullptr;
        if(value.simple){value.originalValues[13]=qint64{1};}else if(const auto* files=info->find("files");files&&files->type==core::Node::Type::List){value.originalValues[13]=QVariant::fromValue(static_cast<qint64>(files->children.size()));}}
      if(attributes){if(const auto* links=attributes->find("filelinks2")){if(const auto encoded=core::encodeValue(*links,storage::compatibleConfigLimits())){if(auto parsed=core::Document::parse(*encoded,storage::compatibleConfigLimits())){value.links=std::move(*parsed);}}}}
      if(attributes){if(const auto* name=attributes->find("displayname");name&&name->type==core::Node::Type::Bytes&&!name->bytes.empty()&&name->bytes.size()<=65536){value.displayName=text(std::string(name->bytes));}}
      if(stamp==primaryStamp(hash)&&primaryRow&&!primaryRow->recoveredBackup()){value.stamp=stamp;}
      const auto [found,inserted]=primaryPresentations.insert_or_assign(key,std::move(value));(void)inserted;return &found->second;
    };
    const auto displayedRoute=[&](const core::TorrentHash& hash){
      if(source.cacheWarning){return Route::Unsupported;}
      const auto cached=cachedAttributes.find(hash.hex());
      if(cached!=cachedAttributes.end()){
        for(const auto key:{"attributes","azp","azpp"}){
          if(const auto* section=cached->second->find(key);section&&section->type!=core::Node::Type::Dictionary){return Route::Unsupported;}
        }
        if(const auto* attributes=cached->second->find("attributes")){return classifyRoute(attributes->find("networks"));}
      }
      const auto* primary=primaryPresentation(hash);return primary?primary->route:Route::Unsupported;
    };
    if(opening){opening(0,source.items.size());}
    const auto mainlineEstimates=engine?engine->mainlineScrapeEstimates():std::vector<engine::MainlineScrapeEstimate>{};
    const auto trackerResponses=engine?engine->libraryTrackerResponses():std::vector<engine::LibraryTrackerResponse>{};
    readMagnets();
    checkpoint(stop);
    const bool guardedFileReview=fileRecoveryRequired||storage::hasPendingFileOperation(lease);
    auto result = std::make_shared<ui::LibraryPresentation>();result->units=displayUnits;result->inlineDetails=inlineDetails;result->confirmExit=confirmExit;result->detailsOnDoubleClick=detailsOnDoubleClick;result->startupDone=startupDone;result->startupTotal=startupTotal;
    result->revision = revision = reserveLibraryRevision();
    std::map<std::string, int> counts;
    std::set<std::string> pauses;
    for (const auto& pause : source.pauses) { pauses.insert(pause.hash.hex()); }
    for (const auto& item : source.items) { if (item.saved.hash) { ++counts[item.saved.hash->hex()]; } }
    std::map<std::string, engine::ManagedDownloadStatus> live;
    if (engine) { for (auto& item : engine->managedDownloads()) { live.emplace(item.hash.hex(), std::move(item)); } }
    std::map<std::string, engine::PendingMagnetStatus> magnets;
    {
      for (auto& item : pendingMagnets()) {
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
      bool complete=false;
      if(i2pOptions&&pureI2p(*item.saved.hash)){const auto found=i2pFacts.find(item.saved.hash->hex());if(found==i2pFacts.end()||!found->second.checked||!found->second.failure.empty()){classified=false;break;}complete=found->second.selectedComplete;}
      else{const auto found=live.find(item.saved.hash->hex());if(found==live.end()||!found->second.checked||found->second.failure){classified=false;break;}complete=found->second.selectedComplete;}
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
    std::size_t presentedCount=0;
    for (const auto& row : source.items) {
      if((presentedCount%64)==0){checkpoint(stop);if(opening){opening(presentedCount,source.items.size());}}
      ++presentedCount;
      const auto& saved = row.saved;
      const auto hash = saved.hash ? saved.hash->hex() : std::string{};
      QString name = text(saved.saveFile.value_or(std::filesystem::path(saved.saveDirectory).filename().string()));
      std::optional<qint64> size;
      std::optional<bool> assumedComplete;
      const auto active=live.find(hash);
      const bool checking=active!=live.end()&&active->second.phase==engine::ManagedPhase::Checking;
      const auto previous=known.find(hash);
      QString trackerName=previous==known.end()?QString{}:previous->second.trackerName;
      std::optional<ui::SavedSelectedValues> savedSelected;
      if (row.cached) {
        const auto& cached = *row.cached;
        const auto selected = core::projectSavedDndProgress(cached.bytes, saved.completedBytes, cached.savedDnd);
        auto selectedBytes=selected.selectedBytes;if(!selectedBytes&&selected.cachedHasDnd==std::optional<bool>{false}){selectedBytes=cached.bytes;}
        savedSelected = ui::SavedSelectedValues{.bytes = selectedBytes, .remaining = selected.selectedRemaining,
            .percentThousandths = selected.percentThousandths, .defaults = selected.defaults, .diagnostics = selected.diagnostics};
        if (cached.displayName && !cached.displayName->empty()) { name = text(*cached.displayName); }
        else if (cached.utf8Name && !cached.utf8Name->empty()) { name = text(*cached.utf8Name); }
        else if (name.isEmpty() && cached.name) { name = text(*cached.name); }
        size = cached.bytes;
        assumedComplete=selected.controllerNoDiskCompleteExcludingDnd;
      } else if (const auto found = known.find(hash); found != known.end()) {
        name = found->second.name;
        size = found->second.bytes;
        assumedComplete=found->second.assumedComplete;
      }
      if(!row.cached&&saved.hash){
        if(const auto* primary=primaryPresentation(*saved.hash)){
          size=primary->bytes;if(primary->displayName){name=*primary->displayName;}
          const auto selected=core::projectSavedDndProgress(size,saved.completedBytes,primary->dnd);assumedComplete=selected.controllerNoDiskCompleteExcludingDnd;
          auto selectedBytes=selected.selectedBytes;if(!selectedBytes&&selected.cachedHasDnd==std::optional<bool>{false}){selectedBytes=size;}
          savedSelected=ui::SavedSelectedValues{.bytes=selectedBytes,.remaining=selected.selectedRemaining,.percentThousandths=selected.percentThousandths,.defaults=selected.defaults,.diagnostics=selected.diagnostics};
        }
      }
      if(saved.hash){
        const auto entry=cachedAttributes.find(hash);
        if(entry!=cachedAttributes.end()&&entry->second->find("au")){
          trackerName=trackerDisplay(entry->second->find("au"),entry->second->find("ag"));
        }else if(const auto* primary=primaryPresentation(*saved.hash)){
          trackerName=primary->trackerName;
        }
      }
      if(!assumedComplete&&size&&saved.completedBytes&&*size>=0&&*saved.completedBytes>=0){assumedComplete=*saved.completedBytes>=*size;}
      if(checking&&previous!=known.end()&&previous->second.assumedComplete){assumedComplete=previous->second.assumedComplete;}
      if(active!=live.end()&&active->second.checked&&!checking&&!active->second.recheck){assumedComplete=active->second.selectedComplete;}
      known.insert_or_assign(hash,Known{name,size,assumedComplete,trackerName});
      const bool paused = pauses.contains(hash);
      const bool stopped = saved.startup == core::StartupState::Stopped;
      QString state = paused ? (schedulerPaused.contains(hash)?SessionBridge::tr("Paused — Speed Scheduler"):SessionBridge::tr("Paused")) : stopped ? SessionBridge::tr("Stopped")
          : saved.startup == core::StartupState::Queued ? SessionBridge::tr("Queued") : SessionBridge::tr("Waiting");
      const bool unique = !hash.empty() && counts[hash] == 1;
      const auto selectedRoute=unique?displayedRoute(*saved.hash):Route::Unsupported;
      if (unique) { currentHashes.insert(hash); }
      const auto key = unique ? hash : hash + ":" + std::to_string(saved.sourceIndex);
      quint16 actions = 0;
      const bool retained = active != live.end() && active->second.retained;
      loadVerifiedSeedIntents();
      if(active!=live.end()&&active->second.allocationMode!=core::TorrentAllocationMode::None){if(active->second.allocationComplete&&!active->second.failure){allocationPending.erase(hash);}else{allocationPending.insert_or_assign(hash,active->second.allocationMode);}}
      if (active != live.end() && active->second.totalPieces > 0) {
        size = active->second.bytes; // Actual admitted metadata survives handle retirement.
        known.insert_or_assign(hash, Known{name, size, assumedComplete,trackerName});
      }
      const auto metadata = magnets.find(hash);
      const bool magnetRetained = metadata != magnets.end() && metadata->second.retained;
      const bool retiring = magnetRetained || (retained && (active->second.phase == engine::ManagedPhase::Stopping || active->second.failure));
      if (retained) {
        state = managedState(active->second);
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
        }
      }
      if (admissionFailures.contains(hash) || (active != live.end() && active->second.failure)) { state = SessionBridge::tr("Error"); }
      if (engine && engine->managedAvailable() && unique && saved.persistent && !retiring && (selectedRoute==Route::Public||(selectedRoute==Route::Mixed&&mixedCapable()))) {
        if (!retained || paused || (selectedRoute==Route::Mixed&&(!mixedStatus.contains(hash)||!mixedStatus.at(hash).active))) { actions |= actionBit(core::DownloadAction::Start); }
        if (!stopped || retained || paused) { actions |= actionBit(core::DownloadAction::Stop); }
        if (!paused && !stopped) { actions |= actionBit(core::DownloadAction::Pause); }
        if (paused) { actions |= actionBit(core::DownloadAction::Resume); }
        if (!saved.forceStart) { actions |= actionBit(core::DownloadAction::ForceStart); }
        else if(selectedRoute==Route::Public&&(active==live.end()||(!active->second.verificationHeld&&active->second.phase!=engine::ManagedPhase::Checking&&active->second.phase!=engine::ManagedPhase::Allocating))){actions|=actionBit(core::DownloadAction::CancelForceStart);}
        actions |= actionBit(core::DownloadAction::Queue);
        if (!movePools.empty()) {
          actions |= actionBit(core::DownloadAction::MoveTop) | actionBit(core::DownloadAction::MoveUp)
              | actionBit(core::DownloadAction::MoveDown) | actionBit(core::DownloadAction::MoveBottom);
        }
      }
      if(allocationPending.contains(hash)){
        actions=0;if(unique&&saved.persistent&&engine&&engine->managedAvailable()){if(active==live.end()||!active->second.failure){actions=actionBit(retained?core::DownloadAction::Stop:core::DownloadAction::Start);}}
        if(retained&&active->second.phase==engine::ManagedPhase::Allocating){state=tr("Allocating %1 / %2 bytes").arg(active->second.allocationBytes).arg(active->second.allocationTotalBytes);}
        else if(!retained&&!admissionFailures.contains(hash)&&(active==live.end()||!active->second.failure)){state=tr("Allocation pending — use Start to retry");}
      }
      const bool i2pRow=i2pOptions&&unique&&selectedRoute==Route::I2p;
      if(i2pRow&&saved.persistent&&!fileRecoveryRequired){
        actions=0;const bool activeI2p=i2p&&i2pPackets.contains(hash);
        if(!activeI2p||paused){actions|=actionBit(core::DownloadAction::Start);}
        if(!stopped||activeI2p||paused){actions|=actionBit(core::DownloadAction::Stop);}
        if(!paused&&!stopped){actions|=actionBit(core::DownloadAction::Pause);}
        if(paused){actions|=actionBit(core::DownloadAction::Resume);}
        actions|=actionBit(core::DownloadAction::Queue);
        if(!saved.forceStart){actions|=actionBit(core::DownloadAction::ForceStart);}
        if(!movePools.empty()){actions|=actionBit(core::DownloadAction::MoveTop)|actionBit(core::DownloadAction::MoveUp)|actionBit(core::DownloadAction::MoveDown)|actionBit(core::DownloadAction::MoveBottom);}
        if(activeI2p){state=i2pPackets.at(hash).state;}

      }
      if(guardedFileReview){actions=0;}
      auto completed = saved.completedBytes && *saved.completedBytes >= 0 ? saved.completedBytes : std::nullopt;
      if (retained) { completed = active->second.completed; size = active->second.bytes; }
      if(i2pRow&&i2p&&i2pPackets.contains(hash)){completed=i2pPackets.at(hash).verifiedBytes;}
      auto received=saved.downloaded,sent=saved.uploaded;
      if(retained){received=active->second.cumulativeReceived;sent=active->second.cumulativeSent;}
      if(i2pRow&&i2pFacts.contains(hash)){received=i2pFacts.at(hash).cumulativeReceived;sent=i2pFacts.at(hash).cumulativeSent;}
      const auto savedRoot=(saved.legacySavePath||!saved.saveFile?std::filesystem::path(saved.saveDirectory):std::filesystem::path(saved.saveDirectory)/ *saved.saveFile).string();
      QString savePath=text(savedRoot);const core::Node* links=nullptr;bool simple=false;
      if(const auto cached=cachedAttributes.find(hash);cached!=cachedAttributes.end()){
        if(const auto* attrs=cached->second->find("attributes")){links=attrs->find("filelinks2");}
        if(const auto* flag=cached->second->find("simple");flag&&flag->type==core::Node::Type::Integer){simple=flag->integer==1;}
        else if(links&&links->type==core::Node::Type::List&&!links->children.empty()&&saved.hash){if(const auto* primary=primaryPresentation(*saved.hash)){simple=primary->simple;}}
      }else if(saved.hash){if(const auto* primary=primaryPresentation(*saved.hash)){simple=primary->simple;links=primary->links?&primary->links->root():nullptr;}}
      if(simple&&links&&links->type==core::Node::Type::List&&!links->children.empty()){
        const std::array<std::string,1> bases{savedRoot};if(const auto selected=compat::resolveOriginalFileLinks(links,bases);selected&&!selected->byIndex.empty()&&selected->byIndex[0]){savePath=text(selected->byIndex[0]->target);}
      }
      std::array<QVariant,18> originalValues{};
      if(const auto cached=cachedAttributes.find(hash);cached!=cachedAttributes.end()){
        originalValues=originalColumns(cached->second->find("attributes"));
        if(row.cached&&row.cached->files){originalValues[13]=QVariant::fromValue(static_cast<qint64>(*row.cached->files));}
      }else if(saved.hash){if(const auto* primary=primaryPresentation(*saved.hash)){originalValues=primary->originalValues;}}
      originalValues[2]=displayTags[hash].join(QStringLiteral(", "));
      if(saved.creationTime&&!originalValues[6].isValid()){originalValues[6]=QVariant::fromValue(static_cast<qint64>(*saved.creationTime));}
      if(saved.uploadLimit){originalValues[8]=QVariant::fromValue(static_cast<qint64>(*saved.uploadLimit));}
      auto goodReceived=received;
      if(goodReceived&&*goodReceived>=0){
        for(const auto bad:{saved.discarded.value_or(0),saved.hashFailed.value_or(0)}){*goodReceived-=std::min<qint64>(*goodReceived,std::max<qint64>(0,bad));}
        if(active!=live.end()){for(const auto bad:{active->second.discardedBytes.value_or(0),active->second.hashFailedBytes.value_or(0)}){*goodReceived-=std::min<qint64>(*goodReceived,std::max<qint64>(0,bad));}}
        if(sent&&*sent>=0){originalValues[0]=*goodReceived==0?std::numeric_limits<double>::infinity():static_cast<double>(std::min<long double>(2147483646.0L,std::floor(static_cast<long double>(*sent)*1000/static_cast<long double>(*goodReceived)))/1000);}
      }
      if(assumedComplete==std::optional<bool>{false}){originalValues[7]=QVariant{};}
      originalValues[1]=retained?tr("Awaiting tracker"):tr("Stopped");
      if(active!=live.end()){
        const auto& current=active->second;
        if(!current.originalResumeDecision.empty()&&reportedResumeDecisions.insert(current.originalResumeDecision).second){qInfo("Resume import diagnostic: %s",current.originalResumeDecision.c_str());}
        switch(current.trackerState){case 1:originalValues[1]=tr("Announcing");break;case 2:originalValues[1]=tr("Working");break;case 3:originalValues[1]=QVariantMap{{QStringLiteral("text"),tr("Tracker error")},{QStringLiteral("detail"),text(current.trackerErrorDetail)}};break;default:break;}
        if(current.seedingRank){originalValues[15]=*current.seedingRank;}
        if(current.queueSourceFacts&&current.queueSourceFacts->swarm&&current.queueSourceFacts->swarm->success){originalValues[3]=current.queueSourceFacts->swarm->seeds;originalValues[4]=current.queueSourceFacts->swarm->peers;}
        if(current.queueTargetStatistics&&current.queueTargetStatistics->etaSeconds){originalValues[9]=QVariant::fromValue(static_cast<qint64>(*current.queueTargetStatistics->etaSeconds));}
        if(current.secondsSinceDownload){originalValues[10]=QVariant::fromValue(static_cast<qint64>(*current.secondsSinceDownload));}
        if(current.queueIdleUploadSeconds){originalValues[11]=*current.queueIdleUploadSeconds;}
        if(current.retained&&current.phase==engine::ManagedPhase::Checking&&current.checkingProgressPpm){++result->activeChecks;}
      }
      result->items.push_back({.key = text(key), .hash = text(hash), .name = name, .state = state,
          .bytes = size, .completed = completed,
          .received = received, .sent = sent,
          .canContinue = !guardedFileReview && peerPort != 0 && unique && saved.persistent && !stopped && !paused && !retained && !magnetRetained && !i2pOptions && selectedRoute==Route::Public,
          .savedPosition = saved.position, .savedSelected = std::move(savedSelected), .actionMask = actions,
          .savePath=std::move(savePath),
          .assumedComplete=assumedComplete, .trackerName=trackerName,.originalValues=std::move(originalValues),.goodReceived=goodReceived});
      if(active!=live.end()){result->items.back().resumeStatus=text(active->second.originalResumeDecision);}
      result->items.back().canRecheck=normalNetwork&&!coexistence()&&!privateOnly()&&engine&&engine->managedAvailable()&&unique&&saved.persistent&&saved.hash->bytes().size()==20&&selectedRoute==Route::Public&&!fileRecoveryRequired&&!recoveryDrainStarted&&!pendingTargets.contains(hash)&&!pendingIntents.contains(hash)&&!verifiedSeedPending.contains(hash)&&!allocationPending.contains(hash)&&!recheckTargets.contains(hash)
          &&(active==live.end()||(!active->second.verificationHeld&&!active->second.recheck&&active->second.allocationMode==core::TorrentAllocationMode::None&&active->second.phase!=engine::ManagedPhase::Checking&&active->second.phase!=engine::ManagedPhase::Stopping));
      if(recheckTargets.contains(hash)){result->items.back().canContinue=false;result->items.back().actionMask=0;result->items.back().state=tr("Force Recheck");}
      if(retained){result->items.back().providerAdmission=active->second.admission;result->items.back().providerGeneration=columnLayoutSession;}
      if(i2pRow&&i2pFacts.contains(hash)){result->items.back().providerAdmission=i2pFacts.at(hash).admission;result->items.back().providerGeneration=i2pFacts.at(hash).generation;}
      if(i2pRow){result->items.back().networkStatus=SessionBridge::tr("Network: I2P only");
        if(i2pPackets.contains(hash)&&!i2pPackets.at(hash).failure.isEmpty()){result->items.back().networkStatus+='\n'+i2pPackets.at(hash).failure;}
        if(i2pFacts.contains(hash)&&i2pFacts.at(hash).completionPositionUnavailable){result->items.back().networkStatus+='\n'+SessionBridge::tr("Automatic seed positioning is not available in this preview.");}
      }
      if(unique&&selectedRoute==Route::Unsupported){result->items.back().networkStatus=SessionBridge::tr("This saved network selection is not supported yet. It has been preserved.");}
      else if(unique&&selectedRoute==Route::Mixed){
        result->items.back().networkStatus=SessionBridge::tr("Network: Public + I2P");
        if(mixedStatus.contains(hash)){const auto& value=mixedStatus.at(hash);
          result->items.back().networkStatus+='\n'+(value.active?SessionBridge::tr("I2P active"):value.failure.empty()?SessionBridge::tr("Connecting — I2P"):SessionBridge::tr("I2P unavailable: %1. Use Start to retry.").arg(text(value.failure)));
        }else if(!mixedCapable()){result->items.back().networkStatus+='\n'+SessionBridge::tr("The mixed I2P provider is unavailable; the saved selection was preserved.");}
      }
      else if(coexistence()&&!i2pRow){result->items.back().networkStatus=SessionBridge::tr("Network: Public");}
      if(admissionFailures.contains(hash)){result->items.back().networkStatus+='\n'+text(admissionFailures.at(hash).message);}
      if(unique&&!magnetRetained&&!paused&&!stopped&&!admissionFailures.contains(hash)){
        const auto tracker=std::ranges::find_if(trackerResponses,[&](const auto& item){return item.hash.hex()==hash
            &&item.admission==(retained?active->second.admission:std::uint64_t{0});});
        if(tracker!=trackerResponses.end()&&tracker->response.type!=core::QueueOriginalScrapeType::Unknown&&tracker->response.content){
          result->items.back().trackerScrape=ui::TrackerScrapeValues{tracker->response.content->seeds,tracker->response.content->peers,
              tracker->observedWallMillis,tracker->response.type==core::QueueOriginalScrapeType::Error};
        }
        const auto estimate=std::ranges::find_if(mainlineEstimates,[&](const auto& item){return item.hash.hex()==hash;});
        if(!retained&&estimate!=mainlineEstimates.end()){result->items.back().mainlineEstimate=ui::MainlineEstimateValues{
            estimate->counts.seeds,estimate->counts.peers,estimate->startedWallMillis};}
      }
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
    std::erase_if(primaryPresentations,[&](const auto& value){return !currentHashes.contains(value.first);});
    if(opening){opening(source.items.size(),source.items.size());}
    return result;
  }
};

SessionBridge::SessionBridge(QObject* parent, CountryProviderFactory factory, AzureusServiceFactory azureusFactory, SchedulerFactory schedulerFactory, std::optional<I2pServiceOptions> i2pOptions, RssFetchFactory rssFactory, SubscriptionFetchFactory subscriptionFactory, SubscriptionAcquisitionFactory acquisitionFactory, SubscriptionFetchFactory automaticFeed, SubscriptionAcquisitionFactory automaticMetainfo, MetasearchFetchFactory metasearch, MetasearchMetainfoFactory metasearchMetainfo, std::vector<std::pair<std::string,int>> mainlineBootstrap)
    : QObject(parent), countryProviderFactory_(std::move(factory)), azureusServiceFactory_(std::move(azureusFactory)), i2pOptions_(std::move(i2pOptions)), schedulerFactory_(std::move(schedulerFactory)), rssFetchFactory_(std::move(rssFactory)), metasearchFetchFactory_(std::move(metasearch)), metasearchMetainfoFactory_(std::move(metasearchMetainfo)), subscriptionFetchFactory_(std::move(subscriptionFactory)), subscriptionAcquisitionFactory_(std::move(acquisitionFactory)), automaticSubscriptionFetchFactory_(std::move(automaticFeed)), automaticSubscriptionAcquisitionFactory_(std::move(automaticMetainfo)) {
#ifdef BIGLYBTC_HOST_GENERAL_CHATS
  chatController_=new GeneralChatController(this);swarmController_=new SwarmDiscoveryController(this);
#endif
  networkIdentityController_=new NetworkIdentityController(this);connect(networkIdentityController_,&NetworkIdentityController::ready,this,&SessionBridge::runtimeMetricsReady);connect(this,&SessionBridge::columnLayoutSessionReady,networkIdentityController_,&NetworkIdentityController::beginSession);
  mainlineBootstrap_=std::move(mainlineBootstrap);
  qRegisterMetaType<ui::SubscriptionKey>();qRegisterMetaType<ui::SubscriptionCatalogKey>();qRegisterMetaType<ui::SubscriptionCatalogPresentation>();qRegisterMetaType<ui::SubscriptionCommandKey>();qRegisterMetaType<ui::SubscriptionCommand>();qRegisterMetaType<ui::SubscriptionPresentation>();
  qRegisterMetaType<ui::RssCatalogRequest>();qRegisterMetaType<ui::RssCatalogPresentation>();qRegisterMetaType<ui::RssFetchKey>();qRegisterMetaType<ui::RssFetchCommand>();qRegisterMetaType<ui::RssFetchPresentation>();
  qRegisterMetaType<ui::MetasearchCatalogKey>();qRegisterMetaType<ui::MetasearchCatalogPresentation>();qRegisterMetaType<ui::MetasearchQueryKey>();qRegisterMetaType<ui::MetasearchQueryCommand>();qRegisterMetaType<ui::MetasearchPresentation>();qRegisterMetaType<ui::MetasearchDownloadKey>();qRegisterMetaType<ui::MetasearchDownloadCommand>();qRegisterMetaType<ui::MetasearchDownloadPresentation>();qRegisterMetaType<ui::MetasearchMagnetSubmission>();
  qRegisterMetaType<ui::RecheckKey>();qRegisterMetaType<ui::RecheckRequest>();qRegisterMetaType<ui::RecheckPresentation>();
  qRegisterMetaType<ui::PublicationCatalogKey>();qRegisterMetaType<ui::PublicationCatalogPresentation>();qRegisterMetaType<ui::PublicationCommand>();qRegisterMetaType<ui::PublicationConfirmation>();qRegisterMetaType<ui::PublicationPresentation>();
  qRegisterMetaType<ui::I2pPresentation>();
  qRegisterMetaType<ui::DownloadHistoryCommand>();
  qRegisterMetaType<ui::DownloadHistoryCommandPresentation>();
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
  qRegisterMetaType<ui::ManualTagCommand>();
  qRegisterMetaType<ui::ManualTagCommandPresentation>();
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
      rssDispatchInterrupted();if(!guard){return;}subscriptionsDispatchInterrupted();if(!guard){return;}metasearchDispatchInterrupted();if(!guard){return;}publicationDispatchInterrupted();if(!guard){return;}
      if(guiCategoryCommand_){
        auto packet=std::make_shared<ui::CategoryCommandPacket>();
        packet->session=guiCategoryCommand_->session;packet->request=guiCategoryCommand_->request;packet->kind=guiCategoryCommand_->kind;packet->requestedTarget=guiCategoryCommand_->target;
        packet->status=result.error()==core::WorkError::Cancelled?ui::CategoryCommandStatus::Cancelled:ui::CategoryCommandStatus::Rejected;
        packet->failure=result.error()==core::WorkError::Cancelled?tr("Cancelled"):tr("Session command failed");
        deliverCategoryCommand(packet);if(!guard){return;}
      }
      if(guiManualTagCommand_){
        auto packet=std::make_shared<ui::ManualTagCommandPacket>();
        packet->session=guiManualTagCommand_->session;packet->request=guiManualTagCommand_->request;packet->kind=guiManualTagCommand_->kind;
        packet->status=result.error()==core::WorkError::Cancelled?ui::ManualTagCommandStatus::Cancelled:ui::ManualTagCommandStatus::Rejected;
        packet->failure=result.error()==core::WorkError::Cancelled?tr("Cancelled"):tr("Session command failed");
        deliverManualTagCommand(packet);if(!guard){return;}
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
      const auto completed=idlePoll_->result().get();
      idlePoll_.reset();
      if(!completed&&completed.error()!=core::WorkError::Cancelled
          &&(!idlePollFailurePosted_||!idlePollFailurePosted_->load())){
        reportIdlePollFailure(idlePollSession_,QStringLiteral("Session worker failed"));
      }else if(completed&&idlePollSession_==publishedColumnLayoutSession_){
        idleFailureSession_=0;idleFailureMessage_.clear();
      }
      idlePollFailurePosted_.reset();
    }
    const auto reported=std::make_shared<std::atomic_bool>(false);
    auto poll = worker_.submit([this,reported](std::stop_token stop) -> core::WorkResult {
      const auto session=host_?host_->columnLayoutSession:quint64{0};
      try{
      if (!stop.stop_requested() && host_) {
        pollSubscriptionAcquisition();take(storage::requireNoPendingSubscriptionAcquisition(host_->lease));pollRssFetch();pollMetasearch();pollMetasearchDownload();if(!host_->metasearchProviderActive()){pollAutomaticSubscriptionQueue();if(!host_->subscriptionAcquisition){pollSubscriptions();}}
        if(host_->recoveryDrainStarted){throw Failure{{storage::ErrorCode::Busy,"Explicit close without checkpoint is incomplete; retained owners may remain live"}};}
        if((storage::hasPendingCategoryDeletion(host_->lease)||storage::hasPendingFileOperation(host_->lease))){
          // Poll may complete metadata imports or write retirement checkpoints.
          // Leave existing owners intact; this is NOT a stopped receipt.
          if(host_->categoryRetainedOwners()){
            throw Failure{{storage::ErrorCode::Conflict,"Profile recovery is pending. Background checkpoint/import work paused; existing network and payload activity may remain live"}};
          }
          return {};
        }
        if(host_->i2pOptions){host_->pollI2p();host_->pollMixed();host_->pollPrivateMagnets();}
        if(!host_->engine){pollPublications();}
        publishAzureus(); // Consume older RTT receipts before advancing source rate buckets.
        if (host_->engine) {
          const auto startupBefore=host_->startupDone;const bool startupChanged=host_->advanceStartup(stop,[this,session](std::size_t done,std::size_t total){
            if(done==1||done==total||done%64==0){QMetaObject::invokeMethod(this,[this,session,done,total]{if(!profileClosed_&&session==publishedColumnLayoutSession_){emit messageReady(tr("Initialising library: %1 / %2").arg(done).arg(total));}},Qt::QueuedConnection);}
          });
          const bool startupProgressed=startupBefore!=host_->startupDone;
          const auto polled=host_->engine->poll();
#ifdef BIGLYBTC_HOST_GENERAL_CHATS
          if(host_->chats){const auto peers=host_->chats->takeTorrentPeers();if(!peers.empty()){const auto delivered=host_->engine->acceptAzureusPeers(peers);if(delivered){host_->chats->recordDeliveredTorrentPeers(*delivered);}}if(std::chrono::steady_clock::now()>=host_->nextAzureusTargets){host_->nextAzureusTargets=std::chrono::steady_clock::now()+std::chrono::seconds(1);static_cast<void>(host_->chats->updateTorrents(host_->engine->azureusTorrentTargets()));}}
#endif
          pollRecheck();publishDownloadHistoryCompletionNotice(host_->consumeHistoryCompletions(stop));publishMainlineFailure();
          if(!polled&&!host_->engine->managedAvailable()){
            auto detail=QStringLiteral("Engine owner unavailable (poll %1)").arg(static_cast<int>(polled.error()));
            if(const auto listener=host_->engine->lastListenerFailure()){
              detail+=QStringLiteral("; listener socket=%1 operation=%2 error=%3 mixedAccept=%4")
                  .arg(listener->socketType).arg(listener->operation).arg(listener->errorCode).arg(listener->handledAsMixedAccept?1:0);
            }
            reportIdlePollFailure(session,std::move(detail));reported->store(true);
          }
          host_->scheduleCoexistence();tickScheduler();tickAccounting();pollPublications();
          if(startupProgressed&&polled){const auto done=host_->startupDone,total=host_->startupTotal;QMetaObject::invokeMethod(this,[this,session,done,total]{if(!profileClosed_&&session==publishedColumnLayoutSession_){emit messageReady(done<total?tr("Initialising torrents: %1 / %2").arg(done).arg(total):tr("Torrents initialised"));}},Qt::QueuedConnection);}
          auto current = host_->engine->managedDownloads();
          bool changed = startupChanged || startupProgressed || host_->coexistence() || current.size() != host_->lastManagedPhases.size();
          for (const auto& item : current) {
            const Host::ManagedStamp stamp{item.phase,item.queueReason,
                item.firstPriority,item.unavailableQueueInputs,item.forceStart,item.allocationBytes,item.allocationComplete,item.trackerState,item.trackerErrorDetail};
            auto [prior, added] = host_->lastManagedPhases.emplace(item.hash.hex(), stamp);
            changed = changed || added || prior->second != stamp;
            prior->second = stamp;
          }
          std::map<std::string, Host::MagnetStamp> magnets;
          for (const auto& item : host_->pendingMagnets()) {
            const auto previous=host_->lastMagnetPhases.find(item.hash.hex());
            if(item.newlyImported&&item.importGeneration&&item.admission&&!item.failure&&item.phase==engine::MagnetPhase::Imported
                &&(previous==host_->lastMagnetPhases.end()||std::get<0>(previous->second)!=item.admission
                    ||std::get<1>(previous->second)!=engine::MagnetPhase::Imported||std::get<6>(previous->second)!=item.importGeneration)){
              host_->noteConstraintIntake(item.hash);
            }
            magnets.emplace(item.hash.hex(), Host::MagnetStamp{item.admission, item.phase, item.retained,
                item.name, item.failure ? std::optional{item.failure->code} : std::nullopt,
                item.failure ? item.failure->message : std::string{},item.importGeneration});
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
                    item.downloadRate, item.uploadRate, managedState(item)});
              }
            }
          }
        }
        if(host_->i2pOptions){
          publishI2p();
          if(!host_->engine){const auto snapshot=host_->query(stop);const auto session=host_->columnLayoutSession;
            QMetaObject::invokeMethod(this,[this,snapshot,session]{if(!profileClosed_&&session==publishedColumnLayoutSession_){emit snapshotReady(snapshot);}},Qt::QueuedConnection);}
          for(const auto& [hash,p]:host_->i2pPackets){progress({{text(hash),host_->revision},p.verifiedBytes,p.downloadRate,p.uploadRate,p.state});}
        }
        if(host_->tickConstraintTags(stop)){publishManualTags();}
      }
      return {};
      }catch(const Failure& failure){
        const bool cancelled=stop.stop_requested()||failure.error.code==storage::ErrorCode::Cancelled;
        if(!cancelled&&!reported->load()){reportIdlePollFailure(session,QStringLiteral("storage %1: %2")
            .arg(static_cast<int>(failure.error.code)).arg(text(failure.error.message).left(384)));reported->store(true);}
        return std::unexpected(cancelled?core::WorkError::Cancelled:core::WorkError::Failed);
      }catch(...){
        // Arbitrary exception text is not a safe source for profile/key bytes.
        if(!stop.stop_requested()&&!reported->load()){reportIdlePollFailure(session,QStringLiteral("Unexpected session worker failure"));reported->store(true);}
        return std::unexpected(stop.stop_requested()?core::WorkError::Cancelled:core::WorkError::Failed);
      }
    });
    if (poll) { idlePollSession_=publishedColumnLayoutSession_;idlePollFailurePosted_=reported;idlePoll_.emplace(std::move(*poll)); }
    else{reportIdlePollFailure(publishedColumnLayoutSession_,QStringLiteral("Session worker queue is unavailable"));}
  });
  idleMonitor_->start();
}
SessionBridge::~SessionBridge() {
#ifdef BIGLYBTC_HOST_GENERAL_CHATS
  if(chatController_){chatController_->requestStop();}
#endif
  stopRssReceiver();stopSubscriptionReceiver();stopMetasearchReceiver();stopPublicationReceiver();
  idleMonitor_->stop();
  if (current_) { current_->cancel(); }
  worker_.requestShutdown();
}
namespace { QString optionDefaultPath(const storage::ApplicationSettingsSnapshot&); engine::LiveQueueLimits optionQueues(const core::Document&); }

void SessionBridge::publishDownloadHistoryCompletionNotice(QString message){
  if(message.isEmpty()||!host_){return;}const auto session=host_->columnLayoutSession;
  QMetaObject::invokeMethod(this,[this,session,message=std::move(message)]{
    if(!profileClosed_&&session==publishedColumnLayoutSession_){emit messageReady(message);}
  },Qt::QueuedConnection); // Last access after direct receiver may delete Bridge.
}
void SessionBridge::ready(ui::Presentation snapshot, QString message) {
  if (host_ && !host_->intakeDefaultLoaded) {
    host_->intakeDefaultLoaded = true;
    auto settings = storage::readApplicationSettings(host_->lease);
    if (settings) { host_->intakeDefault = optionDefaultPath(*settings); }
  }
  if (host_) {
    const auto mainline=host_->mainlineNotice();
    if(!mainline.isEmpty()){message=message.isEmpty()?mainline:message+QLatin1Char('\n')+mainline;}
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
#ifdef BIGLYBTC_HOST_GENERAL_CHATS
  // Queue AFTER the profile identity above. Publishing earlier lets the GUI
  // reject a valid service as belonging to an unopened/stale profile.
  auto chats=host_?host_->chats:std::shared_ptr<GeneralChats>{};
  QMetaObject::invokeMethod(this,[this,session,service=std::move(chats)]{
    if(!profileClosed_&&!closingSession_&&session==publishedColumnLayoutSession_&&chatController_){chatController_->setService(service);swarmController_->setService(service);}
  },Qt::QueuedConnection);
#endif
}
void SessionBridge::reportIdlePollFailure(quint64 session,QString detail){
  detail=detail.left(512);
  QMetaObject::invokeMethod(this,[this,session,detail=std::move(detail)]{
    if(profileClosed_||!session||session!=publishedColumnLayoutSession_){return;}
    if(idleFailureSession_==session&&idleFailureMessage_==detail){return;}
    idleFailureSession_=session;idleFailureMessage_=detail;
    // Observation only: no busy-state change, modal UI or source/route effect.
    emit messageReady(QStringLiteral("Background update failed: ")+detail);
  },Qt::QueuedConnection);
}
void SessionBridge::failed(QString message) {
  QMetaObject::invokeMethod(this, [this, message = std::move(message)] {
    QPointer<SessionBridge> guard(this);
    rssDispatchInterrupted();if(!guard){return;}subscriptionsDispatchInterrupted();if(!guard){return;}metasearchDispatchInterrupted();if(!guard){return;}publicationDispatchInterrupted();if(!guard){return;}
    // Dispatch may observe cancellation before entering the command body.
    if(guiDownloadHistoryCommand_){
      auto packet=std::make_shared<ui::DownloadHistoryCommandPacket>();
      packet->session=guiDownloadHistoryCommand_->session;packet->request=guiDownloadHistoryCommand_->request;packet->kind=guiDownloadHistoryCommand_->kind;
      packet->side=guiDownloadHistoryCommand_->side;packet->uid=guiDownloadHistoryCommand_->uid;
      packet->status=message==tr("Cancelled")?ui::DownloadHistoryStatus::Cancelled:ui::DownloadHistoryStatus::Rejected;
      packet->failure=message.left(512);deliverDownloadHistoryCommand(packet);if(!guard){return;}
    }
    if(guiCategoryCommand_){
      auto packet=std::make_shared<ui::CategoryCommandPacket>();
      packet->session=guiCategoryCommand_->session;packet->request=guiCategoryCommand_->request;packet->kind=guiCategoryCommand_->kind;packet->requestedTarget=guiCategoryCommand_->target;
      packet->status=message==tr("Cancelled")?ui::CategoryCommandStatus::Cancelled:ui::CategoryCommandStatus::Rejected;
      packet->failure=message.left(8192);deliverCategoryCommand(packet);if(!guard){return;}
    }
    if(guiManualTagCommand_){
      auto packet=std::make_shared<ui::ManualTagCommandPacket>();
      packet->session=guiManualTagCommand_->session;packet->request=guiManualTagCommand_->request;packet->kind=guiManualTagCommand_->kind;
      packet->status=message==tr("Cancelled")?ui::ManualTagCommandStatus::Cancelled:ui::ManualTagCommandStatus::Rejected;
      packet->failure=message.left(8192);deliverManualTagCommand(packet);if(!guard){return;}
    }
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
    try {
      checkpoint(stop);
      if(host_&&host_->recoveryDrainStarted&&!closingOperation){
        // Failed terminal cleanup cannot become an admission or profile edit,
        // nor use dispatch's ordinary fresh-query recovery fallback.
        failed(tr("Finish the explicit close without checkpoint before using this session"));posted->store(true);
        return std::unexpected(core::WorkError::Failed);
      }
      operation(stop);posted->store(true);return {};
    }
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
    if(i2pOptions_&&i2pOptions_->publicCoexistence){throw Failure{{storage::ErrorCode::Unsupported,"Use normal profile startup for the shared Public and I2P queue"}};}
    if (host_) { throw Failure{{storage::ErrorCode::Conflict, "A profile is already open"}}; }
    auto lease = take(storage::ProfileLease::acquire(profile));
    const auto acquisition=recoverSubscriptionAcquisitionBeforeHost(lease,profile);
    if(!acquisition||*acquisition){
      host_=std::make_unique<Host>(profile,0,std::move(lease),reserveColumnLayoutSession());
      host_->normalNetwork=false;host_->i2pOptions=i2pOptions_;host_->fileRecoveryRequired=true;
      ready(host_->query(stop),tr("Interrupted subscription addition needs review. Transfers remain stopped; saved files were preserved."));return;
    }
    // The journal guards ALL original inventory/state/cache candidates. Do
    // not run an unrelated recovery or create/checkpoint an owner first.
    if(storage::hasPendingCategoryDeletion(lease)){
      host_=std::make_unique<Host>(profile,ownedPeerPort,std::move(lease),reserveColumnLayoutSession());
      host_->normalNetwork=false;host_->i2pOptions=i2pOptions_;
      host_->categoryRecoveryStartup=true;host_->fileRecoveryRequired=true;
      ready(host_->query(stop),tr("Interrupted category deletion: open Categories to resume an acknowledged prefix or review preserved files. Transfers remain stopped; close and reopen after resolution."));return;
    }
    if(storage::keepDataRemovalNeedsReview(lease)){
      host_=std::make_unique<Host>(profile,0,std::move(lease),reserveColumnLayoutSession());
      host_->normalNetwork=false;host_->i2pOptions=i2pOptions_;host_->fileRecoveryRequired=true;
      ready(host_->query(stop),tr("Interrupted Keep Data removal requires review. Profile files and payloads were preserved; no removal was replayed. Close this read-only session before external review."));return;
    }
    static_cast<void>(take(storage::recoverOriginalCompaction(lease)));
    const bool reviewOnly=recoverOrReviewReplacement(lease);
    if(!reviewOnly){static_cast<void>(take(recoverTorrentIntakeBeforeHost(lease,profile)));}
    const auto session = reserveColumnLayoutSession();
    host_ = std::make_unique<Host>(profile, ownedPeerPort, std::move(lease), session);
    host_->i2pOptions=i2pOptions_;
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
    const auto openingStarted=std::chrono::steady_clock::now();
    const auto openingStage=[this](QString message){QMetaObject::invokeMethod(this,[this,message=std::move(message)]{if(busy_){emit messageReady(message);}},Qt::QueuedConnection);};
    openingStage(tr("Opening library — checking saved state…"));
    auto lease = take(storage::ProfileLease::acquire(profile));
    const auto acquisition=recoverSubscriptionAcquisitionBeforeHost(lease,profile);
    if(!acquisition||*acquisition){
      host_=std::make_unique<Host>(profile,0,std::move(lease),reserveColumnLayoutSession());
      host_->normalNetwork=false;host_->i2pOptions=i2pOptions_;host_->fileRecoveryRequired=true;
      ready(host_->query(stop),tr("Interrupted subscription addition needs review. Transfers remain stopped; saved files were preserved."));return;
    }
    // The journal guards ALL original inventory/state/cache candidates. Do
    // not run an unrelated recovery or create/checkpoint an owner first.
    if(storage::hasPendingCategoryDeletion(lease)){
      host_=std::make_unique<Host>(profile,0,std::move(lease),reserveColumnLayoutSession());
      host_->normalNetwork=true;host_->i2pOptions=i2pOptions_;
      host_->categoryRecoveryStartup=true;host_->fileRecoveryRequired=true;
      ready(host_->query(stop),tr("Interrupted category deletion: open Categories to resume an acknowledged prefix or review preserved files. Transfers remain stopped; close and reopen after resolution."));return;
    }
    if(storage::keepDataRemovalNeedsReview(lease)){
      host_=std::make_unique<Host>(profile,0,std::move(lease),reserveColumnLayoutSession());
      host_->normalNetwork=false;host_->i2pOptions=i2pOptions_;host_->fileRecoveryRequired=true;
      ready(host_->query(stop),tr("Interrupted Keep Data removal requires review. Profile files and payloads were preserved; no removal was replayed. Close this read-only session before external review."));return;
    }
    static_cast<void>(take(storage::recoverOriginalCompaction(lease)));
    const bool reviewOnly=recoverOrReviewReplacement(lease);
    if(!reviewOnly){static_cast<void>(take(recoverTorrentIntakeBeforeHost(lease,profile)));}
    // Do not let dispatch's repairable-policy fallback publish an ordinary Host
    // after storage recovery failed. Only a validated review gets a read-only Host.
    host_ = std::make_unique<Host>(profile, 0, std::move(lease), reserveColumnLayoutSession());
    host_->normalNetwork = true;host_->i2pOptions=i2pOptions_;
    host_->replacementReviewOnly=reviewOnly;host_->fileRecoveryRequired=reviewOnly;
    if(reviewOnly){ready(host_->query(stop),tr("Profile opened read-only. Select the torrent and review its interrupted file replacement. The uncertain directory will be kept."));return;}
    host_->initializeHistoryRemovalTags(stop); // Original tagmanager.enable is a startup fact.
    auto inventory = storage::ConfigFile::load(host_->lease, "downloads.config");
    if (!inventory && inventory.error().code == storage::ErrorCode::Missing) {
      static_cast<void>(take(storage::ConfigFile::createMissing(host_->lease, "downloads.config", "d9:downloadslee")));
    } else if (!inventory) { throw Failure{inventory.error()}; }
    const auto countProgress=[&](std::size_t done,std::size_t total){openingStage(tr("Opening library — %1 / %2 torrents").arg(done).arg(total));};
    const auto initial=host_->query(stop,countProgress);
    qInfo("Library opening: %zu saved rows read in %lld ms",initial->items.size(),static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-openingStarted).count()));
    QMetaObject::invokeMethod(this,[this,initial]{if(busy_){emit snapshotReady(initial);}},Qt::QueuedConnection);
    openingStage(tr("Opening library — starting network services…"));
    host_->dhtAccounting.emplace(false);
    auto identities=host_->bootstrapNormalI2p(stop);
    if(!identities){
      host_->i2pBootstrapFailure=identities.error();
      if(identities.error().code==storage::ErrorCode::Cancelled||stop.stop_requested()){
        host_->i2pBootstrapFailure=storage::Error{storage::ErrorCode::Conflict,"I2P bootstrap was interrupted; close/reopen the profile to review preserved identity files"};
        throw Failure{identities.error()};
      }
    }
    const auto identityReview=host_->bootstrapReview();
    if(!identityReview.isEmpty()){host_->i2pPacket.state=tr("I2P identity needs review");host_->i2pPacket.failure=identityReview;}
    checkpoint(stop);
    initializePersistentOwner(stop);
    if (!host_->engine && (!host_->i2pOptions||host_->coexistence())) { throw Failure{{storage::ErrorCode::Unsupported, "This build lacks a qualified runtime for the requested networks"}}; }
    const auto source = take(storage::readLibraryView(host_->lease));
    std::set<std::string> paused;
    for (const auto& pause : source.pauses) { paused.insert(pause.hash.hex()); }
    for (const auto& item : source.items) {
      checkpoint(stop);
      const auto& saved = item.saved;
      if (!saved.hash || !saved.persistent || saved.startup == core::StartupState::Stopped
          || paused.contains(saved.hash->hex()) || !host_->currentHashes.contains(saved.hash->hex())) { continue; }
      if(!host_->i2pOptions){host_->startupQueue.push_back(*saved.hash);continue;}
      auto started = host_->startSaved(*saved.hash,saved.forceStart,saved.startup==core::StartupState::Queued);
      if (!started) { host_->admissionFailures.insert_or_assign(saved.hash->hex(), started.error());
        if(host_->i2pOptions){host_->i2pPacket.hash=text(saved.hash->hex());host_->i2pPacket.state=tr("Error — I2P");host_->i2pPacket.failure=text(started.error().message);}
      }
    }
    if(!host_->startupQueue.empty()){
      using StartupPriority=std::tuple<bool,bool,bool,std::int64_t,std::size_t>;
      std::map<std::string,StartupPriority> priority;
      for(const auto& item:source.items){if(item.saved.hash){const auto key=item.saved.hash->hex();const auto known=host_->known.find(key);
        const bool complete=known==host_->known.end()||known->second.assumedComplete.value_or(true);
        priority.emplace(key,StartupPriority{complete,!item.saved.forceStart,item.saved.startup==core::StartupState::Queued,item.saved.position.value_or(INT64_MAX),item.saved.sourceIndex});}}
      std::ranges::stable_sort(host_->startupQueue,[&](const auto& a,const auto& b){return priority.at(a.hex())<priority.at(b.hex());});
      // Interleave the two sorted groups: an unfinished queue must not delay
      // ready seeds, and a forced seed prefix must not starve pending checks.
      auto sorted=std::move(host_->startupQueue);host_->startupQueue.clear();
      const auto boundary=std::ranges::find_if(sorted,[&](const auto& hash){return std::get<0>(priority.at(hash.hex()));});
      std::size_t incomplete=0,complete=static_cast<std::size_t>(boundary-sorted.begin()),endIncomplete=complete;
      while(incomplete<endIncomplete||complete<sorted.size()){if(incomplete<endIncomplete){host_->startupQueue.push_back(sorted[incomplete++]);}if(complete<sorted.size()){host_->startupQueue.push_back(sorted[complete++]);}}
      host_->startupTotal=host_->startupQueue.size();host_->startupDone=0;
    }
    host_->bootstrapMagnets(stop);
    host_->scheduleCoexistence();tickScheduler();
    const auto current=host_->query(stop,countProgress);host_->constraintNormalReady=true;
    ready(current,!identityReview.isEmpty()?tr("I2P identity unavailable; review preserved files and reopen: %1").arg(identityReview):
        host_->admissionFailures.empty()?
          (!host_->i2pOptions?tr("Public network mode. I2P, Tor and Azureus DHT remain inactive; saved settings are preserved."):QString{}):
          tr("Some torrents require attention; saved data was preserved"));
    qInfo("Library opening: ready after %lld ms",static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-openingStarted).count()));
    publishI2p();
  });
}
bool SessionBridge::downloadAction(ui::DownloadTargets targets, core::DownloadAction action) {
  if (profileClosed_ || targets.empty()) { return false; }
  return dispatch([this, targets = std::move(targets), action](std::stop_token stop) {
    if(host_){take(storage::requireNoPendingSubscriptionAcquisition(host_->lease));}
    if(host_&&host_->privateOnly()&&host_->lastSnapshot&&std::ranges::none_of(targets,[&](const auto& target){return host_->pendingTargets.contains(target.hash.toStdString());})){
      host_->i2pCommands(targets,action,stop);publishI2p();ready(host_->query(stop));return;
    }
    if (!host_ || (!host_->engine&&!host_->privateOnly()) || !host_->lastSnapshot) {
      throw Failure{{storage::ErrorCode::Unsupported, "No managed engine is available"}};
    }
    if (host_->fileRecoveryRequired || (storage::hasPendingCategoryDeletion(host_->lease)||storage::hasPendingFileOperation(host_->lease)) || storage::hasPendingOriginalCompaction(host_->lease)) { throw Failure{{storage::ErrorCode::Conflict, "Restart to recover the interrupted file operation before changing the profile"}}; }
    if (take(recoverTorrentIntakeAfterSubscription(host_->lease,host_->profile,[owner=host_.get()](const core::TorrentHash& hash){return owner->subscriptionTargetAvailable(hash);}))) {
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
      for (auto& item : take(engine::readPendingMagnets(host_->lease, host_->networkPolicy,host_->magnetContext()))) {
        freshPending.emplace(item.hash.hex(), std::move(item));
      }
      metadata = host_->pendingMagnets();
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
    for(const auto& hash:hashes){host_->engine->revokeMainlineScrape(hash);}
    // Withdraw EVERY accepted selected ticket before the first target's IO.
    // A failure for A must not leave selected B authorized to auto-publish.
    if(action==core::DownloadAction::Stop||action==core::DownloadAction::Pause){
      for(const auto& hash:hashes){host_->startupWithdrawn.insert(hash.hex());}
    }
    std::set<std::string> cancelledVerification,cancelledAllocation;
    const auto admissions=host_->engine?host_->engine->managedDownloads():std::vector<engine::ManagedDownloadStatus>{};
    for(const auto& hash:hashes){if(std::ranges::any_of(admissions,[&](const auto& item){return item.hash==hash&&item.verificationHeld;})){
      cancelledVerification.insert(hash.hex());
    }}
    for(const auto& hash:hashes){if(std::ranges::any_of(admissions,[&](const auto& item){return item.hash==hash&&item.retained&&item.allocationMode!=core::TorrentAllocationMode::None;})){cancelledAllocation.insert(hash.hex());}}
    for(const auto& hash:hashes){if(cancelledVerification.contains(hash.hex())||cancelledAllocation.contains(hash.hex())){
      const auto cancelled=host_->engine->stopManaged(hash);
      if(!cancelled){failures.push_back(text(cancelled.error().message));}
    }}
    host_->verifiedSeedPending.insert(cancelledVerification.begin(),cancelledVerification.end());
    for (const auto& hash : hashes) {
      checkpoint(stop);
      if (const auto pending = pendingSelection.find(hash.hex()); pending != pendingSelection.end()) {
        std::expected<void, storage::Error> controlled;
        if (action == core::DownloadAction::Start) {
          controlled = host_->startPending(pending->second);
        } else {
          const auto current = host_->pendingMagnets();
          const auto live = std::ranges::find_if(current, [&](const auto& item) { return item.hash == hash; });
          // The engine owns retirement whenever an exact handle is retained.
          // Adapter-only cancellation is safe solely without a metadata handle.
          (void)live;controlled = host_->cancelPending(pending->second);
        }
        if (!controlled) {
          host_->admissionFailures.insert_or_assign(hash.hex(), controlled.error());
          failures.push_back(text(hash.hex()) + QStringLiteral(": ") + text(controlled.error().message));
        } else { host_->admissionFailures.erase(hash.hex()); }
        continue;
      }
      if(host_->i2pOptions&&host_->pureI2p(hash)){
        const auto target=std::ranges::find_if(targets,[&](const auto& t){return identity(t)==hash;});
        try{host_->i2pCommands({*target},action,stop);}catch(const Failure& error){host_->admissionFailures.insert_or_assign(hash.hex(),error.error);failures.push_back(text(error.error.message));}
        continue;
      }
      auto beforeControl=host_->engine->managedDownloads();
      const bool wasChecking=cancelledVerification.contains(hash.hex());
      const bool manual=static_cast<unsigned>(action)<=static_cast<unsigned>(core::DownloadAction::ForceStart)||action==core::DownloadAction::CancelForceStart||wasChecking;
      if(manual){
        auto& revision=host_->manualRevisions[hash.hex()];
        if(revision==std::numeric_limits<std::uint64_t>::max()){throw Failure{{storage::ErrorCode::Conflict,"Manual action revision exhausted"}};}
        ++revision;host_->schedulerPaused.erase(hash.hex());
        if(host_->scheduler){
          const auto revoked=host_->scheduler->revokeForManual(host_->lease,hash);
          if(!revoked){failures.push_back(tr("Scheduler ownership was revoked; cleanup needs attention: %1").arg(text(revoked.error().message)));}
        }
      }
      const auto allocation=take(storage::readAllocationIntent(host_->lease,hash));
      if(allocation&&allocation->state!=storage::AllocationState::Complete){
        const bool active=std::ranges::any_of(beforeControl,[&](const auto& item){return item.hash==hash&&item.retained;});
        std::expected<void,storage::Error> controlled;
        if(action==core::DownloadAction::Start&&!active){controlled=host_->startPublic({.hash=hash,.allocationMode=allocation->mode});}
        else if(action==core::DownloadAction::Stop){controlled=host_->engine->stopManaged(hash);}
        else{controlled=std::unexpected(storage::Error{storage::ErrorCode::Conflict,"Allocation remains stopped; use Start to retry or Stop to cancel"});}
        if(!controlled){host_->admissionFailures.insert_or_assign(hash.hex(),controlled.error());failures.push_back(text(controlled.error().message));}else{host_->admissionFailures.erase(hash.hex());}
        continue;
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
          const auto retry=host_->startPublic({.hash=hash,.verifyExisting=true});
          if(!retry){host_->admissionFailures.insert_or_assign(hash.hex(),retry.error());failures.push_back(text(retry.error().message));}
          continue;
        }
      }
      const bool mixedRow=host_->route(hash)==Host::Route::Mixed;
      if(mixedRow&&(action==core::DownloadAction::Stop||action==core::DownloadAction::Pause)){
        const auto removed=host_->removeMixed(hash);if(!removed){failures.push_back(text(removed.error().message));continue;}
      }
      auto changed = host_->libraryCommand(hash, action, host_->movePools);
      if (!changed) { failures.push_back(text(hash.hex()) + QStringLiteral(": ") + text(changed.error().message)); continue; }
      host_->admissionFailures.erase(hash.hex());
      std::expected<void, storage::Error> controlled;
      auto states = host_->engine->managedDownloads();
      const auto live = std::ranges::find_if(states, [&](const auto& value) { return value.hash == hash; });
      const bool active = live != states.end() && live->retained;
      switch (action) {
        case core::DownloadAction::Stop: controlled = host_->engine->stopManaged(hash); break;
        case core::DownloadAction::CancelForceStart:
          if(active){controlled=host_->engine->clearManagedForceStart(hash);}break;
        case core::DownloadAction::Pause:
          if (active) { controlled = host_->engine->pauseManaged(hash, true); } break;
        case core::DownloadAction::Start: case core::DownloadAction::Resume:
        case core::DownloadAction::Queue: case core::DownloadAction::ForceStart:
          if (changed->pauseAfter || changed->after.startup == core::StartupState::Stopped) { break; }
          if (active) { controlled = host_->engine->queueManaged(hash, changed->after.forceStart,
              changed->after.startup == core::StartupState::Queued); }
          else { controlled = host_->startPublic(
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
      }else{
        if(mixedRow&&active&&(action==core::DownloadAction::Start||action==core::DownloadAction::Resume||action==core::DownloadAction::Queue||action==core::DownloadAction::ForceStart)
            &&(!host_->mixedStatus.contains(hash.hex())||!host_->mixedStatus.at(hash.hex()).active)){host_->retryMixed(hash);}
        static_cast<void>(host_->engine->rearmMainlineScrape(hash));
      }
    }
    host_->coexistQueue.invalidatePositions();host_->scheduleCoexistence();publishI2p();
    ready(host_->query({}), failures.empty() ? tr("Download command applied") : failures.join(QLatin1Char('\n')));
  });
}
bool SessionBridge::torrentImport(QString metadata, QString destination, bool start) {
  if (profileClosed_ || metadata.isEmpty() || destination.isEmpty()) { return false; }
  return dispatch([this, metadata = std::move(metadata), destination = std::move(destination), start](std::stop_token stop) {
    if (!host_ || (!host_->i2pOptions && (!host_->engine || !host_->engine->managedAvailable()))) {
      throw Failure{{storage::ErrorCode::Unsupported, "No managed engine is available"}};
    }
    checkpoint(stop);
    if (host_->fileRecoveryRequired || (storage::hasPendingCategoryDeletion(host_->lease)||storage::hasPendingFileOperation(host_->lease)) || storage::hasPendingOriginalCompaction(host_->lease)) { throw Failure{{storage::ErrorCode::Conflict, "Restart to recover the interrupted file operation before changing the profile"}}; }
    static_cast<void>(take(recoverTorrentIntakeAfterSubscription(host_->lease,host_->profile,[owner=host_.get()](const core::TorrentHash& hash){return owner->subscriptionTargetAvailable(hash);})));
    const auto metadataPath=std::filesystem::path(metadata.toUtf8().toStdString());
    const auto filenameContext=take(storage::readOriginalFilenameContext(host_->lease));
    const auto inspected=take(storage::inspectTorrentIntake(metadataPath,stop,filenameContext.conversions,filenameContext.decoderShowAll));
    const auto hash=inspected.hash();
    storage::IntakeHistoryOutcome history;QString historyWarning;
    const auto import=[&]()->std::expected<void,storage::Error>{auto imported=storage::importTorrent(host_->lease,
        std::filesystem::path(metadata.toUtf8().toStdString()),
        std::filesystem::path(destination.toUtf8().toStdString()), !start, nullptr, false,
        host_->privateOnly()?storage::TorrentIntakeNetwork::I2p:storage::TorrentIntakeNetwork::Public,host_->normalNetwork?&history:nullptr);if(!imported){return std::unexpected(imported.error());}if(*imported!=hash){return std::unexpected(storage::Error{storage::ErrorCode::Conflict,"Imported torrent identity changed"});}return {};};
    take(host_->publishTarget(hash,import));historyWarning=host_->consumeHistoryIntake(std::move(history),stop);host_->noteConstraintIntake(hash);
    const auto details = engine::queryTorrent(host_->lease, hash);
    if (details) { host_->rememberKnown(hash.hex(),text(details->name),details->bytes); }
    QString message = historyWarning.isEmpty()?tr("Torrent added"):historyWarning;
    if (start) {
      auto admitted = host_->startSaved(hash);
      if (!admitted) {
        host_->admissionFailures.insert_or_assign(hash.hex(), admitted.error());
        message = historyWarning.isEmpty()?text(admitted.error().message):historyWarning+QStringLiteral("\n")+text(admitted.error().message);
      }
    }
    publishI2p();ready(host_->query({}), message);
  });
}
bool SessionBridge::previewTorrentIntake(ui::TorrentIntakeRequest request) {
  if(profileClosed_||!request.session||request.session!=publishedColumnLayoutSession_||!request.request){return false;}
  return dispatch([this,request=std::move(request)](std::stop_token stop){
    auto packet=std::make_shared<ui::TorrentIntakePacket>();packet->source=request;
    try {
      if(!host_||request.session!=host_->columnLayoutSession){throw Failure{{storage::ErrorCode::Conflict,"The profile changed; load the torrent again"}};}
      if(host_->intakeRequest&&request.request<=host_->intakeRequest->request){throw Failure{{storage::ErrorCode::Conflict,"This file preview request is stale"}};}
      packet->i2pOnly=host_->privateOnly();packet->i2pAvailable=host_->coexistence();packet->mixedAvailable=host_->mixedCapable();
      host_->closeMetasearchDownload();host_->intakeRequest=request;host_->intakeMetadata.reset();
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
      if(host_->fileRecoveryRequired||(storage::hasPendingCategoryDeletion(host_->lease)||storage::hasPendingFileOperation(host_->lease))
          ||storage::hasPendingOriginalCompaction(host_->lease)||storage::hasPendingTorrentIntake(host_->lease)){
        throw Failure{{storage::ErrorCode::Conflict,"Restart to finish pending storage recovery before importing"}};
      }
      if(host_->metasearchDownloadCommand){
        if(!host_->metasearchReviewCurrent(submission.request)||submission.start||submission.forceStart||submission.paused||submission.verifyExisting
            ||submission.allocation!=core::TorrentAllocationMode::None||submission.network!=ui::TorrentIntakeNetwork::Public){
          throw Failure{{storage::ErrorCode::Conflict,"This search review expired or requests an unsupported start/network mode; search again"}};
        }
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
      if(!core::validTorrentAllocationMode(submission.allocation)){throw Failure{{storage::ErrorCode::InvalidFormat,"Invalid allocation mode"}};}
      const bool allocating=submission.allocation!=core::TorrentAllocationMode::None;
      if(host_->coexistence()&&(submission.verifyExisting||allocating)){throw Failure{{storage::ErrorCode::Unsupported,"Verification and allocation are not yet available in the shared Public and I2P queue"}};}
      if(allocating&&(submission.start||submission.forceStart||submission.paused||submission.verifyExisting||!selected)){throw Failure{{storage::ErrorCode::InvalidFormat,"Allocation requires stopped/unforced all-selected intent"}};}
      const bool paused=selected&&submission.paused;
      if(paused&&(submission.start||submission.forceStart)){throw Failure{{storage::ErrorCode::InvalidFormat,"Paused import conflicts with start/force intent"}};}
      const bool start=submission.start&&selected&&!paused;
      if(submission.network!=ui::TorrentIntakeNetwork::Public&&submission.network!=ui::TorrentIntakeNetwork::I2p&&submission.network!=ui::TorrentIntakeNetwork::Mixed){throw Failure{{storage::ErrorCode::InvalidFormat,"Invalid requested torrent network"}};}
      if(submission.network==ui::TorrentIntakeNetwork::I2p&&!host_->i2pOptions){throw Failure{{storage::ErrorCode::Unsupported,"The I2P provider is unavailable"}};}
      const bool mixedNetwork=submission.network==ui::TorrentIntakeNetwork::Mixed;
      if(mixedNetwork&&!host_->mixedCapable()){throw Failure{{storage::ErrorCode::Unsupported,"The mixed I2P provider is unavailable"}};}
      const bool privateI2p=host_->privateOnly()||submission.network==ui::TorrentIntakeNetwork::I2p;
      if(privateI2p&&(submission.forceStart||submission.verifyExisting||allocating||submission.goal!=core::ExistingVerificationGoal::FullTorrent)){throw Failure{{storage::ErrorCode::Unsupported,"The private I2P importer supports Queued, Stopped and Paused only"}};}
      const bool force=start&&submission.forceStart;const bool queued=start&&!force;
      if(!privateI2p&&(start||submission.verifyExisting||allocating)&&(!host_->engine||!host_->engine->managedAvailable())){throw Failure{{storage::ErrorCode::Unsupported,"The engine is unavailable; add this torrent stopped instead"}};}
      checkpoint(stop); // No payload/network work happens before the explicit commit.
      const auto hash=host_->intakeMetadata->hash();
      storage::IntakeHistoryOutcome history;
      const auto import=[&]()->std::expected<void,storage::Error>{
        std::optional<storage::ProfileSourceAccess> sourceGate;
        std::stop_source importCancellation;
        std::stop_callback dispatchCancellation(stop,[&]{importCancellation.request_stop();});
        std::optional<std::stop_callback<std::function<void()>>> resultCancellation;
        auto importStop=stop;
        if(host_->metasearchDownloadCommand){
          resultCancellation.emplace(host_->metasearchDownloadCommand->receiver->get_token(),[&]{importCancellation.request_stop();});
          importStop=importCancellation.get_token();
          if(!host_->metasearchReviewCurrent(submission.request)){return std::unexpected(storage::Error{storage::ErrorCode::Conflict,"The search source or query changed before import"});}
          auto current=host_->metasearchResultHandle->current(host_->lease,host_->columnLayoutSession);
          auto access=host_->lease.sourceAccess();
          if(!current||!access||access->revision()!=*current||host_->metasearchDownloadCommand->receiver->stop_requested()){return std::unexpected(storage::Error{storage::ErrorCode::Conflict,"The search source changed at publication"});}
          sourceGate.emplace(std::move(*access));
        }
        auto imported=storage::importTorrentWithFileOptions(host_->lease,*host_->intakeMetadata,
          std::filesystem::path(submission.destination.toUtf8().toStdString()),!start,submission.files,nullptr,force,importStop,submission.limits,submission.sequentialDownload,queued,paused,submission.trackers,std::move(saveName),submission.verifyExisting,std::move(submission.fileDestinations),submission.goal,submission.allocation,privateI2p?storage::TorrentIntakeNetwork::I2p:mixedNetwork?storage::TorrentIntakeNetwork::Mixed:storage::TorrentIntakeNetwork::Public,host_->normalNetwork?&history:nullptr);if(!imported){return std::unexpected(imported.error());}if(*imported!=hash){return std::unexpected(storage::Error{storage::ErrorCode::Conflict,"Imported torrent identity changed"});}return {};};
      take(host_->publishTarget(hash,import));
      result->imported=true;result->hash=text(hash.hex());host_->intakeMetadata.reset();result->failure=host_->consumeHistoryIntake(std::move(history),stop);host_->noteConstraintIntake(hash);
      const auto details=engine::queryTorrent(host_->lease,hash);
      if(details){host_->rememberKnown(hash.hex(),text(details->displayName&&!details->displayName->empty()?*details->displayName:details->name),details->bytes);}
      if(start||submission.verifyExisting||allocating){
        if(allocating){host_->allocationPending.insert_or_assign(hash.hex(),submission.allocation);}
        if(submission.verifyExisting){host_->verifiedSeedPending.insert(hash.hex());}
        const auto admitted=privateI2p?host_->startI2p(hash):host_->startPublic(
            {.hash=hash,.ownedLoopbackPeerPort=(submission.verifyExisting||allocating)?std::uint16_t{0}:host_->peerPort,.forceStart=force,.queued=queued,.verifyExisting=submission.verifyExisting,.allocationMode=submission.allocation});
        if(admitted){result->started=!submission.verifyExisting&&!allocating;result->verificationPending=submission.verifyExisting;result->allocationPending=allocating;host_->admissionFailures.erase(hash.hex());}
        else{host_->admissionFailures.insert_or_assign(hash.hex(),admitted.error());if(!result->failure.isEmpty()){result->failure+=QStringLiteral("\n");}result->failure+=(allocating?tr("Torrent added stopped; allocation could not start: %1"):submission.verifyExisting?tr("Torrent added stopped; existing-data verification could not start: %1"):tr("Torrent added; download could not start: %1")).arg(text(admitted.error().message));}
      }
    }catch(const Failure& error){result->failure=text(error.error.message);}
    catch(const std::exception&){result->failure=tr("Torrent import did not finish; inspect recovery status before retrying");}
    if(host_&&result->imported&&host_->metasearchDownloadCommand){
      auto download=std::make_shared<ui::MetasearchDownloadPacket>();download->key=host_->metasearchDownloadCommand->key;
      download->state=ui::MetasearchDownloadState::Finished;download->message=tr("Torrent added stopped after review.");
      host_->closeMetasearchDownload(false);postMetasearchDownload(download);
    }
    if(host_&&storage::hasPendingTorrentIntake(host_->lease)){host_->fileRecoveryRequired=true;result->failure+=tr(" Restart to recover the accepted import.");}
    ui::TorrentIntakeResultPresentation packet=result;
    QMetaObject::invokeMethod(this,[this,packet]{
      if(!profileClosed_&&packet->session==publishedColumnLayoutSession_){emit torrentIntakeFinished(packet);}
    },Qt::QueuedConnection);
    if(host_){publishI2p();ready(host_->query({}),result->failure.isEmpty()?(result->allocationPending?tr("Torrent added stopped; allocating new files without downloading"):result->verificationPending?tr("Torrent added stopped; checking existing data before seeding"):tr("Torrent added with the selected file choices")):result->failure);}
    else{failed(result->failure);}
  });
}

bool SessionBridge::magnetImport(QString uri, QString destination) {
  if (profileClosed_ || uri.isEmpty() || destination.isEmpty()) { return false; }
  return dispatch([this, uri = std::move(uri), destination = std::move(destination)](std::stop_token stop) {
    if (!host_ || ((!host_->engine||!host_->engine->managedAvailable())&&!host_->privateOnly())) {
      throw Failure{{storage::ErrorCode::Unsupported, "No managed engine is available"}};
    }
    checkpoint(stop);
    if (host_->fileRecoveryRequired || (storage::hasPendingCategoryDeletion(host_->lease)||storage::hasPendingFileOperation(host_->lease)) || storage::hasPendingOriginalCompaction(host_->lease)) { throw Failure{{storage::ErrorCode::Conflict, "Restart to recover the interrupted file operation before changing the profile"}}; }
    static_cast<void>(take(recoverTorrentIntakeAfterSubscription(host_->lease,host_->profile,[owner=host_.get()](const core::TorrentHash& hash){return owner->subscriptionTargetAvailable(hash);})));
    const auto intent = take(engine::parseMagnetIntent(uri.toUtf8().toStdString(),
        std::filesystem::path(destination.toUtf8().toStdString()), false, host_->networkPolicy,host_->magnetContext()));
    const auto library = take(storage::readLibraryView(host_->lease));
    for (const auto& item : library.items) {
      if (item.saved.hash && *item.saved.hash == intent.hash) {
        throw Failure{{storage::ErrorCode::Conflict, "This magnet is already in the library"}};
      }
    }
    const auto existing = take(engine::readPendingMagnets(host_->lease, host_->networkPolicy,host_->magnetContext()));
    const auto prior = std::ranges::find_if(existing, [&](const auto& value) { return value.hash == intent.hash; });
    for (const auto& active : host_->pendingMagnets()) {
      if (active.hash == intent.hash && active.retained) {
        throw Failure{{storage::ErrorCode::Conflict, "This magnet is already resolving or retiring"}};
      }
    }
    bool sameUri=prior==existing.end()||prior->uri==intent.uri;
    if(!sameUri&&prior->network==engine::MagnetNetwork::I2p&&intent.network==engine::MagnetNetwork::I2p){
      // The reader validated this exact submitted/effective-URI witness. Java
      // sees explicit net=I2P; a repeat paste may still use the submitted URI.
      const auto row=core::Document::parse(prior->sourceEntry);
      const auto* other=row?row->find("other_metadata"):nullptr;
      const auto* submitted=other?other->find("BiglyBTC.pending.submitted_uri"):nullptr;
      sameUri=submitted&&submitted->type==core::Node::Type::Bytes&&submitted->bytes==intent.uri;
    }
    if (prior != existing.end() && (!sameUri || prior->requested != intent.requested)) {
      throw Failure{{storage::ErrorCode::Conflict, "A different magnet request already owns this identity"}};
    }
    checkpoint(stop); // From persistence onward, failures retain the original recovery row.
    engine::PendingMagnet persisted;
    take(host_->publishPending(intent.hash,[&]()->std::expected<void,storage::Error>{
      auto saved=prior==existing.end()?engine::queuePendingMagnet(host_->lease,intent,host_->networkPolicy,host_->magnetContext()):engine::attachPendingMagnetDestination(host_->lease,*prior,*intent.destination,host_->networkPolicy,host_->magnetContext());
      if(!saved){return std::unexpected(saved.error());}persisted=std::move(*saved);return {};
    }));
    const auto admitted = host_->startPending(persisted);
    if (!admitted) { host_->admissionFailures.insert_or_assign(persisted.hash.hex(), admitted.error()); }
    else { host_->admissionFailures.erase(persisted.hash.hex()); }
    ready(host_->query({}), admitted ? tr("Resolving magnet metadata") : text(admitted.error().message));
  });
}
bool SessionBridge::filePolicy(ui::Target target, ui::FileIndices selected, int priority, bool skipped) {
  if (profileClosed_ || selected.empty()) { return false; }
  return dispatch([this, target = std::move(target), selected = std::move(selected), priority, skipped](std::stop_token stop) {
    if(host_&&host_->recheckTargets.contains(target.hash.toStdString())){throw Failure{{storage::ErrorCode::Busy,"Wait for the actual recheck to retire before changing file policy"}};}
    if(host_){host_->requireI2pIdle();}
    if (!host_ || !host_->engine || !host_->engine->managedAvailable()) {
      throw Failure{{storage::ErrorCode::Unsupported, "No managed engine is available"}};
    }
    if (host_->fileRecoveryRequired || (storage::hasPendingCategoryDeletion(host_->lease)||storage::hasPendingFileOperation(host_->lease)) || storage::hasPendingOriginalCompaction(host_->lease)) { throw Failure{{storage::ErrorCode::Conflict, "Restart to recover the interrupted file operation before changing the profile"}}; }
    if (take(recoverTorrentIntakeAfterSubscription(host_->lease,host_->profile,[owner=host_.get()](const core::TorrentHash& hash){return owner->subscriptionTargetAvailable(hash);}))) {
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
        const auto polled=host_->engine->poll();publishDownloadHistoryCompletionNotice(host_->consumeHistoryCompletions(stop));
        if(!polled){throw Failure{{storage::ErrorCode::Io,"Cannot certify file-selection retirement"}};}
        std::this_thread::sleep_for(std::chrono::milliseconds{20});
      }
    }
    checkpoint(stop);
    auto changed=storage::setFilePolicies(host_->lease,hash,policies);
    if(!changed){host_->fileRecoveryRequired=host_->categoryRecoveryStartup||storage::hasPendingFileOperation(host_->lease)||storage::hasPendingOriginalCompaction(host_->lease);throw Failure{changed.error()};}
    host_->filePlan.reset();host_->filePlanPacket.reset();
    if(readmit){
      auto admitted=host_->startPublic({hash,host_->peerPort,force,queued,paused});
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
    if(host_){host_->requireI2pIdle();}
    if (!host_) { throw Failure{{storage::ErrorCode::Missing, "No profile is open"}}; }
    host_->accepts(target);
    if (host_->engine) {
      const auto polled=host_->engine->poll();publishDownloadHistoryCompletionNotice(host_->consumeHistoryCompletions(stop));
      if(!polled){throw Failure{{storage::ErrorCode::Io, "The engine cannot confirm safe idle storage; close the profile before reclaiming space"}};}
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
    host_->fileRecoveryRequired = host_->categoryRecoveryStartup||storage::hasPendingFileOperation(host_->lease)
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
    checkpoint(stop);
    try{host.reloadManualTags(stop);}
    catch(const Failure& failure){
      if(failure.error.code==storage::ErrorCode::Cancelled){throw;}
      host.manualTagFailure=text(failure.error.message).left(8192);
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
void SessionBridge::deliverManualTagCommand(ui::ManualTagCommandPresentation packet) {
  if(!packet||!guiManualTagCommand_||packet->session!=publishedColumnLayoutSession_
      ||packet->session!=guiManualTagCommand_->session||packet->request!=guiManualTagCommand_->request){return;}
  guiManualTagCommand_.reset();
  emit manualTagCommandFinished(std::move(packet)); // Last access: direct receiver may delete Bridge.
}
bool SessionBridge::manualTagCommand(ui::ManualTagCommand request) {
  if(!request.session||!request.request||request.session!=publishedColumnLayoutSession_
      ||busy_||closingSession_||guiManualTagCommand_){return false;}
  switch(request.kind){
    case ui::ManualTagCommandKind::Reload:case ui::ManualTagCommandKind::Create:
    case ui::ManualTagCommandKind::Rename:case ui::ManualTagCommandKind::Remove:
    case ui::ManualTagCommandKind::Visibility:case ui::ManualTagCommandKind::AddMember:
    case ui::ManualTagCommandKind::RemoveMember:case ui::ManualTagCommandKind::SetConstraint:case ui::ManualTagCommandKind::SetGroup:case ui::ManualTagCommandKind::SetColour:break;
    default:return false;
  }
  if(request.name.size()>4096||request.member.hash.size()>64
      ||request.constraintExpression.size()>4096||request.constraintOptions.size()>4096||(request.group&&request.group->size()>4096)){return false;}
  guiManualTagCommand_=request;
  const QPointer<SessionBridge> alive(this);
  const bool accepted=dispatch([this,request=std::move(request)](std::stop_token stop){
    auto packet=std::make_shared<ui::ManualTagCommandPacket>();
    packet->session=request.session;packet->request=request.request;packet->kind=request.kind;
    bool applying=false;
    try{
      if(!host_||host_->columnLayoutSession!=request.session||host_->manualTagEnded){throw Failure{{storage::ErrorCode::Conflict,"This manual-tag profile is no longer active"}};}
      auto& host=*host_;
      if(request.request<=host.lastManualTagRequest){throw Failure{{storage::ErrorCode::Conflict,"This manual-tag request was already consumed"}};}
      host.lastManualTagRequest=request.request;
      checkpoint(stop);
      if(request.kind==ui::ManualTagCommandKind::Reload){host.reloadManualTags(stop);packet->status=ui::ManualTagCommandStatus::Loaded;}
      else{
        if(!request.generation||request.generation!=host.manualTagRevision||!host.manualTagSnapshot.valid()||!host.manualTagStore){throw Failure{{storage::ErrorCode::Conflict,"This manual-tag presentation is stale; reload tags"}};}
        if(host.manualTagRevision==std::numeric_limits<quint64>::max()){throw Failure{{storage::ErrorCode::Unsupported,"Manual-tag presentation revisions are exhausted"}};}
        // Allocate the owning projection alias before a synchronous commit.
        auto holder=std::make_shared<storage::ManualTagSnapshot>();
        auto edit=core::SavedTagEdit(core::SavedTagRemove{request.tagId});
        const auto name=request.name.toUtf8();
        if(request.kind==ui::ManualTagCommandKind::Create||request.kind==ui::ManualTagCommandKind::Rename){
          if(name.size()>4096||QString::fromUtf8(name)!=request.name){throw Failure{{storage::ErrorCode::InvalidFormat,"Manual-tag name must be bounded valid UTF-8"}};}
        }
        std::optional<storage::ProfileSourceAccess> memberAccess;
        switch(request.kind){
          case ui::ManualTagCommandKind::Create:edit=core::SavedTagCreate{name.toStdString(),{}};break;
          case ui::ManualTagCommandKind::Rename:edit=core::SavedTagRename{request.tagId,name.toStdString()};break;
          case ui::ManualTagCommandKind::Remove:break;
          case ui::ManualTagCommandKind::Visibility:edit=core::SavedTagVisibility{request.tagId,request.visible};break;
          case ui::ManualTagCommandKind::SetGroup:{
            std::optional<std::string> group;
            if(request.group){const auto bytes=request.group->toUtf8();
              if(bytes.size()>4096||QString::fromUtf8(bytes)!=*request.group){throw Failure{{storage::ErrorCode::InvalidFormat,"Tag group must be bounded valid UTF-8"}};}
              group=bytes.toStdString();
            }
            edit=core::SavedTagSetGroup{request.tagId,std::move(group)};break;
          }
          case ui::ManualTagCommandKind::SetColour:{
            std::optional<core::SavedTagRgb> colour;
            if(request.colour){colour=core::SavedTagRgb{(*request.colour)[0],(*request.colour)[1],(*request.colour)[2]};}
            edit=core::SavedTagSetColour{request.tagId,colour};break;
          }
          case ui::ManualTagCommandKind::SetConstraint:{
            const auto expression=request.constraintExpression.toUtf8(),options=request.constraintOptions.toUtf8();
            if(expression.size()>4096||options.size()>4096||QString::fromUtf8(expression)!=request.constraintExpression
                ||QString::fromUtf8(options)!=request.constraintOptions){
              throw Failure{{storage::ErrorCode::InvalidFormat,"Constraint configuration must be bounded valid UTF-8"}};
            }
            edit=core::SavedTagSetConstraint{request.tagId,expression.toStdString(),options.toStdString(),request.constraintEnabled};
            // Diagnostic only, before publication. Invalid/unsupported text may
            // be saved exactly; it never grants a membership/provider effect.
            if(!request.constraintEnabled){packet->constraintStatus=tr("Constraint disabled; saved membership is unchanged.");}
            else if(request.constraintExpression.trimmed().isEmpty()){packet->constraintStatus=tr("No active constraint expression; saved membership is unchanged.");}
            else{
              const auto compiled=core::tags::constraints::compile(request.constraintExpression.toStdU16String());
              packet->constraintStatus=compiled?tr("Constraint configured. Automatic changes require supported current facts in a normal profile session."):
                  tr("Constraint configured but unavailable: %1. Saved membership is unchanged.").arg(text(compiled.error().detail).left(512));
            }
            break;
          }
          case ui::ManualTagCommandKind::AddMember:case ui::ManualTagCommandKind::RemoveMember:{
            const auto* tags=host.manualTagSnapshot.tags();const auto* manual=tags?tags->source->find("3"):nullptr;
            const auto* record=manual?manual->find(std::to_string(request.tagId)):nullptr;
            if(record){
              const auto policy=classifyManualTagMembership(*record);
              if(request.kind==ui::ManualTagCommandKind::AddMember?!policy.addAllowed():!policy.removeAllowed()){
                throw Failure{{storage::ErrorCode::Unsupported,"Automatic or ambiguous tag membership cannot be changed in this direction"}};
              }
            }
            host.accepts(request.member);const auto hash=identity(request.member);
            if(host.pendingTargets.contains(hash.hex())){throw Failure{{storage::ErrorCode::Unsupported,"Resolve magnet metadata before changing saved tag membership"}};}
            // Membership is a current saved full-hash binding, never a handle or
            // permission. Keep only filesystem work under this SAME-OFD gate.
            memberAccess.emplace(take(host.lease.sourceAccess()));
            const auto library=take(storage::readLibraryView(host.lease));
            const auto count=std::count_if(library.items.begin(),library.items.end(),[&](const auto& row){return row.saved.hash&&*row.saved.hash==hash;});
            if(count!=1){throw Failure{{storage::ErrorCode::Conflict,"The saved download selection changed; refresh the library"}};}
            if(request.kind==ui::ManualTagCommandKind::AddMember){edit=core::SavedTagAddMember{request.tagId,hash,QDateTime::currentSecsSinceEpoch()};}
            else{edit=core::SavedTagRemoveMember{request.tagId,hash};}break;
          }
          case ui::ManualTagCommandKind::Reload:break;
        }
        checkpoint(stop);applying=true;
        auto committed=take(host.manualTagStore->apply(host.manualTagSnapshot,edit,stop));
        memberAccess.reset(); // No Qt publication or SDK wait while holding FS access.
        packet->status=committed.changed?ui::ManualTagCommandStatus::Saved:ui::ManualTagCommandStatus::Unchanged;
        packet->changed=committed.changed;packet->createdId=committed.createdId;packet->createdUid=committed.createdUid;
        if(committed.changed){
          ++host.manualTagRevision;host.acceptManualTagSnapshot(std::move(committed.snapshot),holder);
        }
      }
    }catch(const Failure& failure){
      const bool cancelled=failure.error.code==storage::ErrorCode::Cancelled;
      packet->status=cancelled?ui::ManualTagCommandStatus::Cancelled:ui::ManualTagCommandStatus::Rejected;
      packet->failure=text(failure.error.message).left(8192);
      if(host_&&host_->columnLayoutSession==request.session){
        if(applying&&!cancelled){host_->beginManualTagRead();host_->manualTagFailure=packet->failure;}
        else if(request.kind==ui::ManualTagCommandKind::Reload){host_->manualTagFailure=packet->failure;}
      }
    }catch(const std::bad_alloc&){
      packet->status=ui::ManualTagCommandStatus::Rejected;packet->failure=tr("Insufficient memory for manual-tag command; reload before retry");
      if(host_&&host_->columnLayoutSession==request.session){host_->beginManualTagRead();host_->manualTagFailure=packet->failure;}
    }
    if(host_&&host_->columnLayoutSession==request.session){host_->manualTagCommandState(*packet);}
    ui::ManualTagStatisticsPresentation statistics;
    try{statistics=manualTagPacket();}catch(...){} // Optional statistics cannot erase an actual commit result.
    // A successful durable save remains Saved even if cancellation arrives now.
    QMetaObject::invokeMethod(this,[this,packet=std::move(packet),statistics=std::move(statistics)]{
      QPointer<SessionBridge> guard(this);
      if(statistics){deliverManualTags(statistics);if(!guard){return;}}
      deliverManualTagCommand(packet);if(!guard){return;}
      completionMonitor_->stop();busy_=false;emit busyChanged(false);
    },Qt::QueuedConnection);
  },false);
  if(alive&&!accepted){guiManualTagCommand_.reset();}return accepted;
}
#include "category_bridge.inc"
#include "download_history_bridge.inc"
#include "rss_sources_bridge.inc"
#include "subscriptions_bridge.inc"
#include "metasearch_bridge.inc"
#include "metasearch_magnet_bridge.inc"
#include "publication_bridge.inc"
#include "recheck_bridge.inc"
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
    if(host_){host_->requireI2pIdle();}
    auto packet = std::make_shared<ui::ColumnLayoutSavePacket>();
    packet->session = session;
    packet->request = request;
    checkpoint(stop);
    if (!host_ || host_->fileRecoveryRequired || (storage::hasPendingCategoryDeletion(host_->lease)||storage::hasPendingFileOperation(host_->lease)) || host_->columnLayoutSession != session || host_->columnBaselineReadStop.stop_requested()
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
    host_->rememberKnown(target.hash.toStdString(),name,value.bytes);
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
    if(host_->i2pOptions||host_->route(identity(target))!=Host::Route::Public){throw Failure{{storage::ErrorCode::Unsupported,"Use the managed Start command for this torrent's saved networks"}};}
    if(host_->fileRecoveryRequired||(storage::hasPendingCategoryDeletion(host_->lease)||storage::hasPendingFileOperation(host_->lease))){throw Failure{{storage::ErrorCode::Conflict,"Profile recovery must finish before starting a transfer"}};}
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
void SessionBridge::publishMainlineFailure() noexcept {
  if(!host_||!host_->engine){return;}
  try{const auto notice=host_->mainlineNotice();if(notice.isEmpty()){return;}
    const auto session=host_->columnLayoutSession;
    QMetaObject::invokeMethod(this,[this,session,notice]{
      if(!profileClosed_&&session==publishedColumnLayoutSession_){emit messageReady(notice);}
    },Qt::QueuedConnection);
  }catch(...){}
}
void SessionBridge::initializePersistentOwner(std::stop_token stop) {
  if (!host_ || host_->privateOnly() || host_->fileRecoveryRequired || (storage::hasPendingCategoryDeletion(host_->lease)||storage::hasPendingFileOperation(host_->lease)) || (host_->peerPort == 0 && !host_->normalNetwork) || host_->engine) { return; }
  checkpoint(stop);
  // Preserve the exact source/runtime guard and default unbound fallback.
  auto owner = engine::PersistentSession::create({1, host_->columnLayoutSession, 1});
  if (!owner) {
    if (owner.error() == engine::AccountingError::DependencyUnqualified) { return; }
    throw Failure{{storage::ErrorCode::Io, "Cannot create the native engine owner"}};
  }
  if (host_->normalNetwork) {
#if defined(BIGLYBTC_HOST_MAINLINE_IDENTITY)
    const auto directory=storage::discoverOriginalMainlineDirectory(host_->lease);
    if(directory){host_->normalMainlineDirectory=*directory;}
    else if(directory.error().code!=storage::ErrorCode::Missing){throw Failure{directory.error()};}
#endif
    if (!host_->socks4CapabilityChecked) {
      // Retain actual provider proof before parsing any possibly invalid saved
      // setting. Options can then repair that setting without starting routes.
      host_->networkPolicy.socks4Capability = engine::verifiedSocks4Capability();
      host_->socks4CapabilityChecked = true;
    }
    auto config = storage::ConfigFile::load(host_->lease, "biglybt.config");
    if (!config && config.error().code != storage::ErrorCode::Missing) { throw Failure{config.error()}; }
    auto policy = (host_->mixedCapable()?engine::readMixedNetworkPolicy:engine::readNetworkPolicy)(config ? &config->document() : nullptr,
        host_->normalNetworkOptions(config ? &config->document() : nullptr));
    if (!policy) { throw Failure{{storage::ErrorCode::Unsupported, "Unsupported network setting: " + policy.error().field}}; }
    auto queues = core::readQueuePolicy(config ? &config->document() : nullptr);
    if (!queues) { throw Failure{{storage::ErrorCode::InvalidFormat,
        "Invalid queue setting: " + queues.error().key}}; }
    if(host_->coexistence()){host_->coexistQueue.configure(*queues);}
    auto configuredQueue = (*owner)->configureQueuePolicy(*queues,configuredSourceSmoothing(config ? &config->document() : nullptr).windowSeconds);
    if (!configuredQueue) { throw Failure{configuredQueue.error()}; }
    auto applied = (*owner)->configureNetwork(*policy);
    if (!applied) { throw Failure{applied.error()}; }
    host_->networkPolicy = std::move(*policy);try{host_->networkIdentityConfig.saved=services::network_identity::saved(config?&config->document():nullptr);}catch(...){host_->networkIdentityConfig.saved.clear();}
    auto live = (*owner)->applyLiveSettings(host_->networkPolicy, config ? optionQueues(config->document()) : engine::LiveQueueLimits{});
    if (!live) { throw Failure{live.error()}; }
    // Optional discovery failure is retained by the actual owner and surfaced
    // below; it never creates a second lease or changes the payload route.
    const auto original=(*owner)->configureOriginalMainlineIdentity(host_->lease);
    if(!original&&(*owner)->mainlineDiscovery().state!=engine::MainlineDiscoveryStatus::State::Unavailable){throw Failure{original.error()};}
  }
  if(host_->mixedCapable()){
    take((*owner)->configureMixedNetworks());
    take((*owner)->configureMixedSourceBinding([this](const auto& source)->std::expected<void,storage::Error>{
      return host_->mixed&&host_->mixedBindingTargets.contains(source->identity().originalKey.hex())?host_->mixed->bind(source):std::expected<void,storage::Error>{};
    }));
  }
  if(host_->coexistence()){
    take((*owner)->configureExternalQueue(true));
    take((*owner)->configureSourcePublication(
      [this](const core::TorrentHash& hash,const auto& f){return host_->publishTarget(hash,f);},
      [this](const auto& f){return host_->publishPositions(f);}));
  }
  if(!mainlineBootstrap_.empty()){take((*owner)->configureMainlineBootstrap(mainlineBootstrap_));}
  if (!(*owner)->startIdle(host_->lease)) {
    throw Failure{{storage::ErrorCode::Io, "Cannot initialize the native idle session"}};
  }
  checkpoint(stop); // Local owning pointer aborts/joins before a cancelled publication.
  host_->engine = std::move(*owner);
#ifdef BIGLYBTC_HOST_GENERAL_CHATS
  if(host_->normalNetwork&&!host_->chats){
    auto factory=host_->engine->chatTransport(host_->lease);
    if(factory){auto opened=GeneralChats::create(std::move(*factory),host_->columnLayoutSession);if(opened){host_->chats=std::move(*opened);}}
  }
#endif
  if(host_->normalNetwork){
    const auto session=host_->columnLayoutSession;auto* actual=host_->engine.get();
    // Optional payload-history retention is opt-in; ordinary engine clients
    // keep their original restart/forget lifecycle when no consumer exists.
    const auto completionInterest=actual->configureHistoryCompletions(host_->lease);
    if(!completionInterest){host_->downloadHistoryPaused=true;host_->downloadHistoryIntakeNotice=tr("Transfer history is unavailable; torrent startup remains independent.");}
    const auto configured=actual->configureMagnetCompletionHook(host_->lease,[this,session,actual](storage::IntakeHistoryOutcome outcome)->std::optional<storage::Error>{
      if(!host_||host_->columnLayoutSession!=session||host_->engine.get()!=actual
          ||host_->recoveryDrainStarted||!host_->normalNetwork){
        return storage::Error{storage::ErrorCode::Conflict,"Import saved; magnet history owner/session changed"};
      }
      return host_->consumeMagnetHistoryIntake(std::move(outcome));
    });
    if(!configured){host_->downloadHistoryPaused=true;host_->downloadHistoryIntakeNotice=tr("Download history is unavailable; torrent startup remains independent.");}
  }
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
    bool serviceConfigured=host_->azureusEnabled;
    if (host_->azureus) { observation = host_->azureus->observation(); }
#ifdef BIGLYBTC_HOST_GENERAL_CHATS
    const auto sharedDht=host_->chats?host_->chats->snapshot():nullptr;
    serviceConfigured=serviceConfigured||bool(sharedDht);
    if(!observation&&sharedDht){
      AzureusServiceObservation actual;actual.sequence=sharedDht->sequence;actual.current=!sharedDht->closing&&!sharedDht->closed;actual.state=actual.current?AzureusServiceState::Running:AzureusServiceState::Closed;
      for(std::size_t lane=0;lane<sharedDht->transports.size();++lane){if(sharedDht->transports[lane]){actual.snapshot=sharedDht->transports[lane];actual.routerSnapshot=sharedDht->routers[lane];break;}}
      observation=std::move(actual);
    }
#endif
    if (host_->dhtAccounting) { host_->dhtAccounting->observe(observation); }
    if (host_->liveSpeedMapper) { host_->liveSpeedMapper->observeAzureus(observation, steadyMillis()); }
    publishLiveSpeed();
    if (host_->azureusPublished && (!observation || observation->sequence == host_->azureusObservationSequence)) { return; }
    if (host_->azureusSequence == std::numeric_limits<quint64>::max()) { return; }
    auto packet = std::make_shared<ui::AzureusStatisticsPacket>();
    packet->session = host_->columnLayoutSession; packet->sequence = host_->azureusSequence + 1;
    packet->current = observation && observation->current;
    if (!serviceConfigured) { packet->state = ui::AzureusStatisticsState::Disabled; }
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
  auto network=std::make_shared<services::network_identity::Input>(host_->networkIdentityConfig);const auto addresses=host_->engine->publicAddresses();for(std::size_t i=0;i<2;++i){network->addresses[i]=QString::fromStdString(addresses[i]);}
#ifdef BIGLYBTC_HOST_GENERAL_CHATS
  if(host_->chats){const auto chats=host_->chats->snapshot();if(chats&&chats->generation==host_->columnLayoutSession&&!chats->closing&&!chats->closed){if(network->addresses[0].isEmpty()){network->addresses[0]=QString::fromStdString(chats->externalV4);}if(network->addresses[1].isEmpty()){network->addresses[1]=QString::fromStdString(chats->externalV6);}}}
#endif
  network->dnsAllowed=host_->normalNetwork&&host_->networkPolicy.mode==engine::NetworkMode::Normal&&host_->networkPolicy.proxy.kind==engine::NetworkProxyKind::None;if(!host_->networkPolicy.ipv6){network->addresses[1].clear();}packet->networkInput=std::move(network);
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
    if (result && (*result)->session == publishedColumnLayoutSession_) { networkIdentityController_->observe(*result); }
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
    if(host_){host_->requireI2pIdle();}
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
bool SessionBridge::closeSession(){return closeSessionImpl(false,0);}
bool SessionBridge::closeSessionWithoutCheckpoint(quint64 session){
  if(!session||session!=publishedColumnLayoutSession_||profileClosed_){return false;}
  return closeSessionImpl(true,session);
}
bool SessionBridge::closeSessionImpl(bool withoutCheckpoint,quint64 expectedSession) {
  stopRssReceiver();stopSubscriptionReceiver();stopMetasearchReceiver();stopPublicationReceiver();stopRecheckReceiver();
  if (busy_ || closingSession_) { return false; }
  closingSession_ = true;
  idleMonitor_->stop();
  QPointer<SessionBridge> guard(this);
  const bool accepted = dispatch([this,withoutCheckpoint,expectedSession](std::stop_token) {
    const auto session = host_ ? host_->columnLayoutSession : quint64{0};
    closeWorkerRecheck();closeWorkerRss();closeWorkerSubscriptions();closeWorkerMetasearch();closeWorkerPublications();
    if(withoutCheckpoint&&(!host_||session!=expectedSession||!host_->recoveryDrainOffered
        ||(!host_->recoveryDrainStarted&&!(storage::hasPendingCategoryDeletion(host_->lease)||storage::hasPendingFileOperation(host_->lease))))){
      throw Failure{{storage::ErrorCode::Conflict,"Close without checkpoint requires this session's actual pending-journal close refusal"}};
    }
    if(withoutCheckpoint){host_->recoveryDrainStarted=true;} // Actual accepted worker attempt; never reset.
    bool joined = true;
    bool released = true;
    ui::NativeSessionPresentation finalObservation;
    QString checkpointFailure;
    QString historyCompletionWarning;
    if(!withoutCheckpoint&&host_&&(host_->recoveryDrainStarted
        ||((storage::hasPendingCategoryDeletion(host_->lease)||storage::hasPendingFileOperation(host_->lease))&&host_->categoryRetainedOwners()))){
      host_->recoveryDrainOffered=true;
      closeFailed(tr("A pending profile journal prevents checkpointing close. Transfers may remain live. You may explicitly close without saving the latest transfer progress."));
      QMetaObject::invokeMethod(this,[this,session]{
        if(!profileClosed_&&!closingSession_&&session==publishedColumnLayoutSession_){emit recoveryCloseOffered(session);}
      },Qt::QueuedConnection);
      return;
    }
    if(host_){
      if(host_->engine){host_->engine->clearMagnetCompletionHook();}
      host_->constraintNormalReady=false;
      if(host_->constraintWorker){host_->constraintWorker->close();}
      std::expected<void,storage::Error> mixedStopped;
      if(withoutCheckpoint&&host_->mixed){
        mixedStopped=host_->mixed->closeWithoutCheckpoint();
        host_->mixedJoined=host_->mixed->joined();
        if(!host_->mixedJoined){throw Failure{{storage::ErrorCode::Busy,"Mixed I2P drain did not join; retained graph requires review"}};}
        host_->mixed.reset();host_->mixedStatus.clear();host_->mixedBindingTargets.clear();
      }else{mixedStopped=host_->stopMixed();}joined=joined&&host_->mixedJoined;
      if(!mixedStopped){checkpointFailure=text(mixedStopped.error().message);}
      std::expected<void,storage::Error> stopped;
      if(withoutCheckpoint&&host_->i2p){
        stopped=host_->i2p->closeWithoutCheckpoint();host_->i2pJoined=host_->i2p->joined();
        if(!host_->i2pJoined){throw Failure{{storage::ErrorCode::Busy,"Private I2P drain did not join; retained owner requires review"}};}
        host_->i2p.reset();host_->i2pPackets.clear();host_->i2pFacts.clear();host_->coexistQueue.privateOwnerChanged();
      }else{stopped=host_->stopI2p();}joined=joined&&host_->i2pJoined;
      if(!stopped){checkpointFailure=text(stopped.error().message);}publishI2p();
    }
#ifdef BIGLYBTC_HOST_GENERAL_CHATS
    if(host_&&host_->chats){host_->chats->requestStop();host_->chats->join();host_->chats.reset();QMetaObject::invokeMethod(this,[this]{if(chatController_){chatController_->setService({});}if(swarmController_){swarmController_->setService({});}},Qt::QueuedConnection);}
#endif
    if (host_ && host_->azureus) {
      joined = static_cast<bool>(host_->azureus->close());
      publishAzureus();
    }
    if(!withoutCheckpoint){refreshDhtAccounting(true);} // Omission path publishes no final accounting.
    if(withoutCheckpoint&&host_&&host_->engine){
      const auto drained=host_->engine->closeWithoutCheckpoint(std::chrono::steady_clock::now()+std::chrono::seconds(5));
      joined=joined&&drained.sessionJoined;released=drained.storageReleased;
      if(!drained.sessionJoined||!drained.storageReleased){throw Failure{{storage::ErrorCode::Busy,"Retained engine drain did not join; close remains incomplete"}};}
      if(drained.diagnostic){checkpointFailure=tr("Closed without saving profile progress; final progress was not certified.");}
    }
    if (!withoutCheckpoint && host_ && host_->engine) {
      host_->engine->clearObservationSink();
      const auto receipt = host_->engine->close(std::chrono::steady_clock::now() + std::chrono::seconds{5});
      historyCompletionWarning=host_->consumeHistoryCompletions({},true);
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
    QMetaObject::invokeMethod(this, [this, session, joined, released, checkpointFailure,historyCompletionWarning,withoutCheckpoint] {
      QPointer<SessionBridge> guard(this);
      completionMonitor_->stop();
      publishedColumnLayoutSession_ = 0; // Reject old-session requests before releasing busy.
      profileClosed_ = true;networkIdentityController_->beginSession(0);
      guiRecheck_.reset();guiRecheckSequence_=0;guiRssCatalog_.reset();guiRssFetch_.reset();rssStarting_.reset();guiPublicationCatalog_.reset();guiPublication_.reset();startingPublication_=false;guiMetasearchCatalog_.reset();guiMetasearch_.reset();guiMetasearchDownload_.reset();startingMetasearch_.reset();startingMetasearchDownload_.reset();
      busy_ = false;
      emit busyChanged(false); // Keep closingSession_ true through this callback.
      if (!guard) { return; }
      closingSession_ = false;
      idleMonitor_->start();
      if (!checkpointFailure.isEmpty()) { emit messageReady(checkpointFailure); if (!guard) { return; } }
      if (!historyCompletionWarning.isEmpty()) { emit messageReady(historyCompletionWarning); if (!guard) { return; } }
      if(withoutCheckpoint){emit sessionClosedWithoutCheckpoint(session,joined,released);}
      else{emit sessionClosed(session, joined, released);} // Final access: receiver may reopen/delete Bridge.
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
void SessionBridge::cancel() { QPointer<SessionBridge> guard(this);stopRecheckReceiver();if(!guard){return;}stopMetasearchReceiver();stopPublicationReceiver();if(!guard){return;} automaticSubscriptionQueueCancelled_.store(true);if(const auto automatic=automaticSubscriptionCancellation_.load()){automatic->request_stop();}if (current_ && !closingSession_) { current_->cancel(); } }

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
          const auto policy = (host_->mixedCapable()?engine::readMixedNetworkPolicy:engine::readNetworkPolicy)(read->source.get(),
              host_->normalNetworkOptions(read->source.get()));
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
    if(host_){host_->requireI2pIdle();}
    auto packet = std::make_shared<ui::OptionsSavePacket>();
    packet->session = submission.session; packet->request = submission.request; packet->revision = submission.revision;
    if (!host_ || host_->fileRecoveryRequired || (storage::hasPendingCategoryDeletion(host_->lease)||storage::hasPendingFileOperation(host_->lease)) || host_->columnLayoutSession != submission.session || !host_->optionsBaseline
        || host_->optionsRevision != submission.revision || host_->optionsReadStop.stop_requested()
        || host_->optionsRevision == std::numeric_limits<quint64>::max()) {
      packet->failure = tr("Reload settings before saving; this snapshot is no longer current");
    } else {
      auto prepared = storage::prepareApplicationSettings(*host_->optionsBaseline, submission.changes);
      if (!prepared) { packet->failure = text(prepared.error().message); }
      else {
        auto policy = (host_->mixedCapable()?engine::readMixedNetworkPolicy:engine::readNetworkPolicy)(&prepared->document(),
            host_->normalNetworkOptions(&prepared->document()));
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
                  if(host_->coexistence()){host_->coexistQueue.configure(*queuePolicy);}
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
    if(host_){host_->requireI2pIdle();}
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
    if(host_){host_->requireI2pIdle();}
    auto result=std::make_shared<ui::SchedulerSavePacket>();result->session=value.session;result->request=value.request;result->revision=value.revision;
    if(!host_||host_->fileRecoveryRequired||(storage::hasPendingCategoryDeletion(host_->lease)||storage::hasPendingFileOperation(host_->lease))||host_->columnLayoutSession!=value.session||!host_->scheduler){result->failure=tr("Reload schedules before saving");}
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
    if(host_->fileRecoveryRequired||(storage::hasPendingCategoryDeletion(host_->lease)||storage::hasPendingFileOperation(host_->lease))||storage::hasPendingOriginalCompaction(host_->lease)){return;}
    ensureScheduler();if(!host_->scheduler){return;}
    if(host_->coexistence()&&host_->i2p&&host_->scheduler->snapshot().settings.enabled){
      throw Failure{{storage::ErrorCode::Unsupported,"Speed Scheduler cannot change the shared profile while I2P torrents are running. Stop I2P torrents before applying scheduled profile changes."}};
    }
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
        seedingOnly=any&&onlySeeds&&host_->pendingMagnets().empty();
      }
      std::vector<core::SettingChange> edits;
      if(plan.uploadKiB){edits.push_back({seedingOnly?core::SettingId::SeedingUploadLimit:core::SettingId::UploadLimit,std::int64_t{*plan.uploadKiB}});}
      if(plan.downloadKiB){edits.push_back({core::SettingId::DownloadLimit,std::int64_t{*plan.downloadKiB}});}
      auto prepared=take(storage::prepareApplicationSettings(source,edits));
      auto requested=(host_->mixedCapable()?engine::readMixedNetworkPolicy:engine::readNetworkPolicy)(&prepared.document(),
          host_->normalNetworkOptions(&prepared.document()));
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
      host_->engine->revokeMainlineScrape(action.hash);
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
        static_cast<void>(host_->engine->rearmMainlineScrape(action.hash));
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
      host_->engine->revokeMainlineScrape(action.target.hash);
      const auto began=host_->scheduler->beginImportedAction(host_->lease,action,*row);
      if(!began){host_->schedulerFailure=text(began.error().message);continue;}
      try{
        std::optional<storage::LibraryCommandResult> publication;
        if(action.kind!=storage::scheduler::ActionKind::SetStopReason){
          const auto command=action.kind==storage::scheduler::ActionKind::Resume?core::DownloadAction::Resume:core::DownloadAction::Start;
          publication=take(host_->libraryCommand(action.target.hash,command));
          const auto& saved=*publication;
          if(!saved.pauseAfter&&saved.after.startup!=core::StartupState::Stopped){
            const auto active=host_->engine->managedDownloads();
            const auto live=std::ranges::find(active,action.target.hash,&engine::ManagedDownloadStatus::hash);
            const auto controlled=live!=active.end()&&live->retained
                ?host_->engine->queueManaged(action.target.hash,saved.after.forceStart,saved.after.startup==core::StartupState::Queued)
                :host_->startPublic({action.target.hash,host_->peerPort,saved.after.forceStart,saved.after.startup==core::StartupState::Queued});
            if(!controlled){throw Failure{controlled.error()};}
          }
          host_->schedulerPaused.erase(action.target.hash.hex());
        }
        if(action.kind==storage::scheduler::ActionKind::SetStopReason&&action.target.paused){host_->schedulerPaused.insert(action.target.hash.hex());}
        static_cast<void>(take(host_->scheduler->finishImportedAction(host_->lease,action,storage::SchedulerActionResult::Succeeded,
            publication?&*publication:nullptr)));
        static_cast<void>(host_->engine->rearmMainlineScrape(action.target.hash));
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

namespace biglybtc::app {
void SessionBridge::publishI2p(){
  if(!host_||!host_->i2pOptions){return;}auto p=std::make_shared<ui::I2pPacket>(host_->i2pPacket);
  p->session=host_->columnLayoutSession;p->sequence=++host_->i2pSequence;
  if(p->state.isEmpty()){p->state=tr("I2P ready — use Start on an I2P-only torrent");}
  QMetaObject::invokeMethod(this,[this,p=std::move(p)]{if(p->session==publishedColumnLayoutSession_){emit i2pReady(p);}},Qt::QueuedConnection);
}
}

namespace biglybtc::app {
void SessionBridge::connectChats(ui::LibraryWindow* window){
 if(!window){return;}
#ifdef BIGLYBTC_HOST_GENERAL_CHATS
 connect(swarmController_,&SwarmDiscoveryController::snapshotReady,window,&ui::LibraryWindow::setSwarmSnapshot);
 connect(swarmController_,&SwarmDiscoveryController::notice,window,&ui::LibraryWindow::setMessage);
 connect(window,&ui::LibraryWindow::swarmRefreshRequested,swarmController_,&SwarmDiscoveryController::refresh);
 connect(window,&ui::LibraryWindow::swarmDiscoverRequested,swarmController_,&SwarmDiscoveryController::discover);
 connect(window,&ui::LibraryWindow::swarmLookupRequested,swarmController_,&SwarmDiscoveryController::lookup);
 connect(window,&ui::LibraryWindow::swarmSettingsRequested,swarmController_,&SwarmDiscoveryController::settings);
 connect(window,&ui::LibraryWindow::swarmReadRequested,swarmController_,&SwarmDiscoveryController::read);
 connect(window,&ui::LibraryWindow::swarmRemoveRequested,swarmController_,&SwarmDiscoveryController::remove);
 connect(window,&ui::LibraryWindow::swarmResetDeletedRequested,swarmController_,&SwarmDiscoveryController::resetDeleted);
 connect(window,&ui::LibraryWindow::swarmLayoutRequested,swarmController_,&SwarmDiscoveryController::layout);
 connect(chatController_,&GeneralChatController::snapshotReady,window,&ui::LibraryWindow::setChatSnapshot);
 connect(chatController_,&GeneralChatController::sendFinished,window,&ui::LibraryWindow::chatSendFinished);
 connect(chatController_,&GeneralChatController::notice,window,&ui::LibraryWindow::setMessage);
 connect(window,&ui::LibraryWindow::chatJoinRequested,chatController_,&GeneralChatController::joinRoom);
 connect(window,&ui::LibraryWindow::chatSelectRequested,chatController_,&GeneralChatController::selectRoom);
 connect(window,&ui::LibraryWindow::chatReadingChanged,chatController_,&GeneralChatController::readingRoom);
 connect(window,&ui::LibraryWindow::chatLeaveRequested,chatController_,&GeneralChatController::leaveRoom);
 connect(window,&ui::LibraryWindow::chatSendRequested,chatController_,&GeneralChatController::send);
 connect(window,&ui::LibraryWindow::chatOptionsRequested,chatController_,&GeneralChatController::options);
 connect(window,&ui::LibraryWindow::chatNicknameModeRequested,chatController_,&GeneralChatController::nicknameMode);
 connect(window,&ui::LibraryWindow::chatNicknameRequested,chatController_,&GeneralChatController::nickname);
#else
 ui::ChatViewSnapshot value;value.availability=tr("This build does not include Public chat transport");window->setChatSnapshot(std::move(value));
#endif
}
}
