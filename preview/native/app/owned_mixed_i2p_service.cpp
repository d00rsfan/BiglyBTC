// SPDX-License-Identifier: GPL-2.0-or-later
#include "owned_mixed_i2p_service.hpp"
#ifndef BIGLYBTC_HOST_I2P_OWNER
#define BIGLYBTC_HOST_I2P_OWNER 0
#endif
#if BIGLYBTC_HOST_I2P_OWNER && __has_include(<libtorrent/biglybtc_mixed_i2p_transport.hpp>)
#include "i2p_saved_settings.hpp"
#include "i2p_discovery_options.hpp"
#include "file_io.hpp"
#include "../engine/mixed_i2p_source.hpp"
#include "../engine/mixed_i2p_transport.hpp"
#include "../services/i2p_provider/bridge.hpp"
#include "../services/i2p_provider/source_permit.hpp"
#include "../services/i2p_provider/pending_magnet_source.hpp"
#include "../engine/pending_metadata_source.hpp"
#include "magnet_intake.hpp"
#include <libtorrent/biglybtc_mixed_i2p_transport.hpp>
#include <QCoreApplication>
#include <QDateTime>
#include <QHostAddress>
#include <QMetaObject>
#include <QThread>
#include <openssl/rand.h>
#include <algorithm>
#include <atomic>
#include <limits>
#include <map>
#include <mutex>

namespace biglybtc::app {
namespace provider=services::i2p::provider;
namespace i2cp=services::i2p::i2cp;
namespace {
using Error=storage::Error;using Code=storage::ErrorCode;
std::unexpected<Error> refused(const char* text){return std::unexpected(Error{Code::Conflict,text});}
// Separate from torrent admissions, and never reset when a graph is recreated.
std::uint64_t nextRouteNonce()noexcept{
  static std::atomic<std::uint64_t> value{0};auto old=value.load(std::memory_order_relaxed);
  while(old!=std::numeric_limits<std::uint64_t>::max()){
    if(value.compare_exchange_weak(old,old+1,std::memory_order_relaxed)){return old+1;}
  }return 0;
}
struct Reservation {std::shared_ptr<const storage::ProfileLease> lease;};
std::shared_ptr<Reservation> reserveProfile(std::shared_ptr<const storage::ProfileLease> lease){
  static std::mutex mutex;static std::vector<std::weak_ptr<Reservation>> current;
  std::lock_guard lock(mutex);std::erase_if(current,[](const auto& weak){return weak.expired();});
  for(const auto& weak:current){if(auto held=weak.lock();held&&held->lease->sharesOwnership(*lease)){return {};}}
  if(current.size()>=64){return {};}auto held=std::make_shared<Reservation>(Reservation{std::move(lease)});current.push_back(held);return held;
}
}
struct OwnedMixedI2pService::Impl {
  struct Entry {std::shared_ptr<provider::ProfileSource> child;std::shared_ptr<const engine::MixedManagedSource> managed;};
  struct Graph final:QObject {
    struct Route {Observation status;std::shared_ptr<provider::ProfileSource> child;std::shared_ptr<const engine::MixedManagedSource> managed;bool pending=false;std::shared_ptr<provider::PendingMagnetSource> magnet;std::shared_ptr<const engine::PendingMetadataSource> metadata;};
    std::shared_ptr<provider::ProfileSource> source;std::shared_ptr<engine::MixedI2pTransport> transport;
    std::optional<services::i2p::ClientIdentity> identity;I2pServiceOptions options;std::optional<services::i2p::dht::Actor::Options> discoveryOptions;
    std::unique_ptr<i2cp::InitialChannel> channel;std::unique_ptr<provider::Bridge> bridge;std::unique_ptr<provider::SourcePermit> permit;
    std::map<std::string,Route> routes;std::string failure;std::optional<Error> closeError;
    bool begun=false,closing=false,configured=false,installQueued=false,startRequested=false,bridgeRequested=false,closeUncertain=false;
    std::uint64_t generation=0,bridgeAdmission=0;
    Graph(std::shared_ptr<provider::ProfileSource> s,std::shared_ptr<engine::MixedI2pTransport> t,
        services::i2p::ClientIdentity key,I2pServiceOptions o,std::optional<services::i2p::dht::Actor::Options> discovery):source(std::move(s)),transport(std::move(t)),identity(std::move(key)),options(std::move(o)),discoveryOptions(std::move(discovery)){
      channel=std::make_unique<i2cp::InitialChannel>(this);
      QObject::connect(channel.get(),&i2cp::InitialChannel::sourceEffectRequested,this,[this]{request();});
      QObject::connect(channel.get(),&i2cp::InitialChannel::dateReceived,this,[this]{if(!closing){startRequested=true;request();}});
      QObject::connect(channel.get(),&i2cp::InitialChannel::leaseSetSubmitted,this,[this]{if(!closing&&!bridge){bridgeRequested=true;request();}});
      QObject::connect(channel.get(),&i2cp::InitialChannel::failed,this,[this]{close("Mixed I2CP transport failed");});
      QObject::connect(channel.get(),&i2cp::InitialChannel::closed,this,[this]{close("Mixed I2CP transport ended");});
    }
    bool needsWork()const{
      if(closing){return false;}
      if(startRequested||channel->sourceEffectsPending()||(bridgeRequested&&channel->ownedTransportIdle())){return true;}
      if(bridge&&bridge->observation().phase==provider::Bridge::Phase::Ready&&!configured&&!installQueued){return true;}
      return configured&&std::any_of(routes.begin(),routes.end(),[](const auto& item){return item.second.pending;});
    }
    void request(){if(begun&&!closing&&permit&&permit->phase()==provider::SourcePermit::Phase::Idle&&needsWork()){permit->request();}}
    bool begin(){
      if(begun||closing||!source->mixedMode()||!source->prepared()){return false;}begun=true;
      permit=std::make_unique<provider::SourcePermit>(source,this);
      const i2cp::InitialChannel::Options router{QHostAddress::LocalHost,options.routerPort,"2.13.0",false,30000,false};
      if(!channel->bindEffectAuthority(source->effectAuthority())||!channel->begin(router)){close("Cannot begin mixed I2CP session");return false;}return true;
    }
    bool configure(void* opaque){
      if(closing||configured||!bridge||!source->mixedMode()){return false;}
      const auto& policy=*static_cast<const libtorrent::BiglybtcMixedI2pTransportPolicyV1*>(opaque);
      auto result=transport->configure(policy);if(!result){failure=result.error().message;return false;}
      generation=policy.generation;configured=true;request();return true;
    }
    void startup(){
      if(closing||!permit){return;}auto revision=permit->take();
      if(!revision){const auto phase=permit->phase();if(phase==provider::SourcePermit::Phase::Failed||phase==provider::SourcePermit::Phase::Stopped){close("Mixed I2P source validation failed");}else{request();}return;}
      struct Ack{provider::SourcePermit& permit;~Ack(){permit.acknowledge();}} ack{*permit};
      if(!source->revisionCurrent(*revision)||!channel->resumeAuthorizedEffects()){close("Mixed I2P current source effect refused");return;}
      if(startRequested){
        startRequested=false;const auto wall=QDateTime::currentMSecsSinceEpoch();
        if(!identity||wall<=0||!channel->createLeaseSession(std::move(*identity),options.sessionOptions,static_cast<std::uint64_t>(wall))){close("Cannot create original mixed I2CP identity session");return;}identity.reset();
      }
      if(bridgeRequested&&channel->ownedTransportIdle()){
        bridgeRequested=false;
        const auto actual=channel->ownedLeaseDestination();
        if(!actual||actual->wire!=source->destination().wire){close("Mixed I2CP identity differs from held original key");return;}
        bridge=std::make_unique<provider::Bridge>(source,*channel,this);
        QObject::connect(bridge.get(),&provider::Bridge::changed,this,[this]{
          if(closing||!bridge){return;}const auto state=bridge->observation();
          if(state.phase==provider::Bridge::Phase::Withdrawn){close(bridge->failureReason());}else{request();}
        });
        if(discoveryOptions){
          auto actual=*discoveryOptions;
          if(std::all_of(actual.localId.begin(),actual.localId.end(),[](auto b){return b==0;})){
            std::array<services::i2p::Byte,14> random{};
            if(RAND_bytes(random.data(),static_cast<int>(random.size()))!=1){close("Cannot derive mixed I2P DHT identity");return;}
            auto id=services::i2p::dht::makeNodeId(source->destination().hash,actual.queryPort,random);
            if(!id){close("Cannot bind mixed I2P DHT identity");return;}actual.localId=*id;
          }
          // begin() must establish the manager before DHT receiver activation.
          discoveryOptions=std::move(actual);
        }
        if(RAND_bytes(reinterpret_cast<unsigned char*>(&bridgeAdmission),sizeof(bridgeAdmission))!=1||!bridgeAdmission
            ||!bridge->begin({options.peerPort,options.peerPort},bridgeAdmission)){close("Cannot begin mixed I2P bridge");return;}
        if(discoveryOptions&&!bridge->beginDiscovery(*discoveryOptions)){close("Cannot begin mixed I2P DHT");return;}
      }
      if(bridge&&bridge->observation().phase==provider::Bridge::Phase::Ready&&!configured&&!installQueued){
        installQueued=true;
        // This is a private internal hook, executed by Bridge only under its
        // fresh one-shot source permit; it never enters filesystem publication.
        if(!bridge->installMixedProvider([this](void* policy){return configure(policy);})){close("Cannot queue mixed I2P provider configuration");return;}
      }
      if(configured){for(auto& [key,route]:routes){(void)key;if(!route.pending){continue;}route.pending=false;
        if(!source->revisionCurrent(*revision)||!routeCurrent(route)){
          route.status.unavailable=true;route.status.failure="Mixed source or engine admission is no longer current";continue;
        }
        const auto nonce=nextRouteNonce();if(!nonce){route.status.unavailable=true;route.status.failure="Mixed route nonce space exhausted";continue;}
        auto admitted=route.metadata?transport->admitMetadata(*route.metadata,generation,nonce):transport->admit(*route.managed,generation,nonce);
        route.status.generation=generation;route.status.routeNonce=nonce;
        if(!admitted){route.status.unavailable=true;route.status.failure=admitted.error().message;continue;}
        if(!source->revisionCurrent(*revision)||!routeCurrent(route)){
          if(route.metadata){(void)transport->revokeMetadata(*route.metadata,generation,nonce);}else{(void)transport->revoke(*route.managed,generation,nonce);}route.status.unavailable=true;route.status.failure="Mixed source changed during route admission";continue;
        }
        route.status.active=true;route.status.unavailable=false;
        requestDiscovery(route,nonce);
      }}
    }

    bool routeCurrent(const Route& route)const{
      if(route.metadata){return route.magnet&&route.magnet->prepared()&&route.metadata->current();}
      return route.child&&route.managed&&route.child->prepared()&&route.managed->current();
    }
    void requestDiscovery(Route& route,std::uint64_t nonce){
      if(!discoveryOptions||!bridge||!routeCurrent(route)){return;}
      const bool allowed=route.metadata?route.magnet->discoveryAllowed():route.child->discoveryAllowed();if(!allowed){return;}
      services::i2p::dht::Id wire{};const auto selected=route.status.hash.bytes();
      if(selected.size()<wire.size()){return;}
      std::transform(selected.begin(),selected.begin()+wire.size(),wire.begin(),[](std::byte b){return std::to_integer<unsigned char>(b);});
      const auto target=route.status.hash;const auto owned=route.managed;const auto metadata=route.metadata;const auto g=generation;
      auto deliver=[this,target,owned,metadata,g,nonce](const auto& peer,std::uint64_t operation){
        auto found=routes.find(target.hex());if(closing||found==routes.end()||!found->second.status.active
          ||found->second.status.generation!=g||found->second.status.routeNonce!=nonce
          ||found->second.managed!=owned||found->second.metadata!=metadata||!routeCurrent(found->second)){return false;}
        return metadata?transport->discoveredPeer(*metadata,g,nonce,peer,operation).has_value():transport->discoveredPeer(*owned,g,nonce,peer,operation).has_value();
      };
      const bool accepted=route.metadata?bridge->discover(route.magnet,target,wire,nonce,false,deliver):bridge->discover(route.child,target,wire,nonce,false,deliver);
      if(!accepted){route.status.failure="Mixed I2P DHT source request refused";}
    }
    std::expected<void,Error> addMetadata(std::shared_ptr<provider::PendingMagnetSource> child,std::shared_ptr<const engine::PendingMetadataSource> metadata){
      if(closing||!child||!metadata||!child->prepared()||!metadata->current()||child->hash()!=metadata->hash()){return refused("Current pending metadata source required");}
      const auto key=metadata->hash().hex();if(routes.contains(key)||routes.size()>=32){return refused("Mixed metadata route already retained or capacity reached");}
      Observation status;status.hash=metadata->hash();status.admission=metadata->admission();status.metadataOnly=true;
      Route route;route.status=std::move(status);route.pending=true;route.magnet=std::move(child);route.metadata=std::move(metadata);
      routes.emplace(key,std::move(route));request();return {};
    }
    std::expected<void,Error> add(std::shared_ptr<provider::ProfileSource> child,std::shared_ptr<const engine::MixedManagedSource> managed){
      if(closing||!child||!managed||!child->prepared()||!managed->current()){return refused("Live prepared mixed source required");}
      const auto hash=managed->identity().originalKey;const auto key=hash.hex();
      if(const auto found=routes.find(key);found!=routes.end()){
        return found->second.managed==managed&&!found->second.status.unavailable?std::expected<void,Error>{}:refused("Previous mixed route must be removed first");
      }
      if(routes.size()>=32){return refused("Mixed member bound32");}
      Observation status;status.hash=hash;status.admission=managed->admission();
      routes.emplace(key,Route{std::move(status),std::move(child),std::move(managed),true,{},{}});request();return {};
    }
    std::expected<void,Error> suspend(const core::TorrentHash& hash){
      const auto found=routes.find(hash.hex());if(found==routes.end()){return {};}
      auto& route=found->second;route.pending=false;if(bridge&&route.status.routeNonce){bridge->cancelDiscovery(route.status.routeNonce);}
      if(!route.status.active){return {};}
      route.status.active=false;
      auto result=route.metadata?transport->revokeMetadata(*route.metadata,route.status.generation,route.status.routeNonce):transport->revoke(*route.managed,route.status.generation,route.status.routeNonce);
      if(!result){route.status.unavailable=true;route.status.failure=result.error().message;}return result;
    }
    void close(const char* reason=nullptr)noexcept{
      if(closing){return;}closing=true;
      try{
        if(reason&&failure.empty()){failure=reason;}source->revoke();
        // Withdrawing affects only the mixed transport. It never aborts the
        // borrowed Public session or saves/removes any of its payloads.
        if(configured){configured=false;auto result=transport->withdraw(generation);if(!result){closeError=result.error();}}
        if(bridge){bridge->close();}if(permit){permit->stop();}channel->close();identity.reset();
        for(auto& [key,route]:routes){(void)key;route.pending=false;route.status.active=false;route.status.unavailable=true;route.status.generation=generation;route.status.failure=failure;}
      }catch(...){closeUncertain=true;source->revoke();if(permit){permit->stop();}if(bridge){bridge->close();}channel->close();}
    }
    void join()noexcept{if(bridge){bridge->joinSource();}if(permit){permit->join();}}
    std::vector<Observation> observations()const{
      std::vector<Observation> result;result.reserve(routes.size());for(const auto& [key,route]:routes){(void)key;auto status=route.status;
        if(!route.metadata&&!route.managed->current()){status.active=false;status.unavailable=true;status.failure="Public engine admission retired";}
        result.push_back(std::move(status));}return result;
    }
    bool event(QEvent* event)override{
      if(event->type()==provider::SourcePermit::wakeEvent()){
        try{const auto phase=permit?permit->phase():provider::SourcePermit::Phase::Stopped;
          if(phase==provider::SourcePermit::Phase::Ready||phase==provider::SourcePermit::Phase::Failed||phase==provider::SourcePermit::Phase::Stopped){startup();}else{request();}
        }catch(...){close("Mixed I2P source continuation failed");}return true;
      }return QObject::event(event);
    }
  };
  std::shared_ptr<provider::ProfileSource> source;std::shared_ptr<Reservation> reservation;
  std::unique_ptr<Graph> graph;QThread thread;QThread* caller=QThread::currentThread();
  struct PendingEntry {std::shared_ptr<provider::PendingMagnetSource> source;std::shared_ptr<const engine::PendingMetadataSource> metadata;};
  std::map<std::string,PendingEntry> pendingPrepared;
  std::map<std::string,Entry> prepared;std::map<std::string,Observation> attempts;mutable std::mutex mutex;mutable std::vector<Observation> last;
  bool closed=false,joined=false;std::optional<Error> closeError;
  Impl(std::shared_ptr<provider::ProfileSource> s,std::shared_ptr<Reservation> r,std::unique_ptr<Graph> g):source(std::move(s)),reservation(std::move(r)),graph(std::move(g)){}
  bool onCaller()const noexcept{return QThread::currentThread()==caller;}
  template<class F>bool onGraph(F&& f)const{
    bool failed=false;const auto invoke=[&]{try{f();}catch(...){failed=true;graph->close("Mixed I2P internal dispatch failed");}};
    if(graph->thread()==QThread::currentThread()){invoke();return !failed;}
    return thread.isRunning()&&QMetaObject::invokeMethod(graph.get(),invoke,Qt::BlockingQueuedConnection)&&!failed;
  }
  std::expected<void,Error> shutdown(){
    if(!onCaller()){return refused("Mixed I2P close requires its Host worker");}if(closed){return closeError?std::expected<void,Error>{std::unexpected(*closeError)}:std::expected<void,Error>{};}
    if(!onGraph([&]{graph->close();last=graph->observations();closeError=graph->closeError;if(graph->closeUncertain){closeError=Error{Code::Io,"Mixed route withdrawal did not complete normally"};}if(graph->thread()!=caller){(void)graph->moveToThread(caller);}})){
      source->revoke();closeError=Error{Code::Io,"Mixed I2P event loop unavailable during close"};
    }
    thread.quit();thread.wait();graph->join();graph.reset();joined=true;closed=true;reservation.reset();
    for(auto& status:last){status.active=false;status.unavailable=true;status.joined=true;}
    return closeError?std::expected<void,Error>{std::unexpected(*closeError)}:std::expected<void,Error>{};
  }
};
OwnedMixedI2pService::OwnedMixedI2pService(std::unique_ptr<Impl> p):impl_(std::move(p)){}
bool OwnedMixedI2pService::available()noexcept{return true;}
std::expected<std::unique_ptr<OwnedMixedI2pService>,storage::Error> OwnedMixedI2pService::create(
    const storage::ProfileLease& lease,I2pServiceOptions options,std::shared_ptr<engine::MixedI2pTransport> transport){
  if(!transport||!options.routerPort||options.peerPort!=6881||!QCoreApplication::instance()||QCoreApplication::closingDown()
      ||QThread::currentThread()==QCoreApplication::instance()->thread()){return refused("Mixed I2P requires its real Public transport, Host worker and original port6881");}
  try{
    struct Bootstrap {std::shared_ptr<provider::ProfileSource> source;std::shared_ptr<Reservation> reservation;services::i2p::ClientIdentity identity;I2pServiceOptions options;std::optional<services::i2p::dht::Actor::Options> discovery;};
    auto bootstrap=[&]()->std::expected<Bootstrap,Error>{
      auto access=lease.sourceAccess();if(!access){return std::unexpected(access.error());}
      auto config=storage::ConfigFile::load(lease,"biglybt.config");if(!config){return std::unexpected(config.error());}
      // Caller supplies RAW options. Explicit fixture/user tunnel properties
      // retain precedence; saved mixed defaults and source share this gate.
      auto actual=readSavedMixedI2pOptions(config->document(),std::move(options));if(!actual){return std::unexpected(actual.error());}
      auto discovery=readI2pDiscoveryOptions(lease,true,*actual);if(!discovery){return std::unexpected(discovery.error());}
      auto source=provider::ProfileSource::createMixedSession(lease);if(!source){return std::unexpected(source.error());}
      auto reserved=reserveProfile((*source)->lease());if(!reserved){return refused("One live mixed I2P graph per profile is required");}
      auto identity=(*source)->takeIdentity();if(!identity){return std::unexpected(identity.error());}
      return Bootstrap{std::move(*source),std::move(reserved),std::move(*identity),std::move(*actual),std::move(*discovery)};
    }(); // Release source access BEFORE Qt startup or any provider operation.
    if(!bootstrap){return std::unexpected(bootstrap.error());}
    auto graph=std::make_unique<Impl::Graph>(bootstrap->source,std::move(transport),std::move(bootstrap->identity),std::move(bootstrap->options),std::move(bootstrap->discovery));
    auto service=std::unique_ptr<OwnedMixedI2pService>(new OwnedMixedI2pService(std::make_unique<Impl>(std::move(bootstrap->source),std::move(bootstrap->reservation),std::move(graph))));auto& s=*service->impl_;
    QObject::connect(&s.thread,&QThread::finished,s.graph.get(),[&s]{
      if(s.graph&&s.graph->thread()==QThread::currentThread()){
        s.graph->close("Mixed I2P event loop ended");(void)s.graph->moveToThread(s.caller);
      }
    },Qt::DirectConnection);
    s.thread.start();if(!s.thread.isRunning()||!s.graph->moveToThread(&s.thread)){(void)s.shutdown();return refused("Cannot start mixed I2P event loop");}
    bool began=false;if(!s.onGraph([&]{began=s.graph->begin();})||!began){(void)s.shutdown();return refused("Cannot begin mixed I2P transport");}return service;
  }catch(...){return std::unexpected(Error{Code::Io,"Cannot create original mixed I2P graph"});}
}

std::expected<void,storage::Error> OwnedMixedI2pService::prepareMagnet(const engine::PendingMagnet& intent){
  auto& s=*impl_;std::lock_guard lock(s.mutex);if(!s.onCaller()||s.closed||s.pendingPrepared.contains(intent.hash.hex())||s.prepared.contains(intent.hash.hex())){return refused("Current unretained mixed pending intent required");}
  auto source=s.source->preparePendingMagnet(intent);if(!source){return std::unexpected(source.error());}
  s.pendingPrepared.emplace(intent.hash.hex(),Impl::PendingEntry{std::move(*source),{}});return {};
}
std::expected<void,storage::Error> OwnedMixedI2pService::bindMetadata(std::shared_ptr<const engine::PendingMetadataSource> metadata){
  auto& s=*impl_;std::lock_guard lock(s.mutex);if(!s.onCaller()||s.closed||!metadata){return refused("Current actual metadata seal required");}
  auto found=s.pendingPrepared.find(metadata->hash().hex());if(found==s.pendingPrepared.end()){return refused("Prepared pending source required");}
  auto result=found->second.source->bindMetadata(metadata);if(result){found->second.metadata=std::move(metadata);}return result;
}
std::expected<void,storage::Error> OwnedMixedI2pService::attachMetadata(std::shared_ptr<const engine::PendingMetadataSource> metadata){
  auto& s=*impl_;std::lock_guard lock(s.mutex);if(!s.onCaller()||s.closed||!metadata){return refused("Current actual metadata seal required");}
  auto found=s.pendingPrepared.find(metadata->hash().hex());if(found==s.pendingPrepared.end()||found->second.metadata!=metadata){return refused("Exact bound pending source required");}
  std::expected<void,Error> result;if(!s.onGraph([&]{result=s.graph->addMetadata(found->second.source,metadata);})){return refused("Mixed metadata event loop unavailable");}return result;
}
std::expected<void,storage::Error> OwnedMixedI2pService::removeMetadata(const core::TorrentHash& hash){
  auto& s=*impl_;std::lock_guard lock(s.mutex);if(!s.onCaller()||s.closed){return refused("Live mixed Host worker required");}
  auto found=s.pendingPrepared.find(hash.hex());if(found==s.pendingPrepared.end()){return {};}
  std::expected<void,Error> result;if(!s.onGraph([&]{result=s.graph->suspend(hash);if(result){s.graph->routes.erase(hash.hex());}})){return refused("Mixed metadata event loop unavailable");}
  if(!result){return result;}auto released=found->second.source->release();if(!released){return released;}s.pendingPrepared.erase(found);s.attempts.erase(hash.hex());return {};
}
std::expected<void,storage::Error> OwnedMixedI2pService::publishPendingMagnet(const core::TorrentHash& hash,const std::function<std::expected<void,storage::Error>()>& operation){
  auto& s=*impl_;if(!s.onCaller()||s.closed){return refused("Live mixed pending publication owner required");}return s.source->publishPendingMagnet(hash,operation);
}
std::expected<void,storage::Error> OwnedMixedI2pService::prepare(const core::TorrentHash& hash){
  auto& s=*impl_;std::lock_guard lock(s.mutex);if(!s.onCaller()||s.closed){return refused("Live mixed Host worker required");}
  if(s.prepared.contains(hash.hex())){return refused("Mixed child already prepared");}
  Observation attempted;attempted.hash=hash;
  auto child=s.source->prepareMixedChild(hash);if(!child){attempted.unavailable=true;attempted.failure=child.error().message;s.attempts[hash.hex()]=std::move(attempted);return std::unexpected(child.error());}
  s.attempts[hash.hex()]=std::move(attempted);s.prepared.emplace(hash.hex(),Impl::Entry{std::move(*child),{}});return {};
}
std::expected<void,storage::Error> OwnedMixedI2pService::bind(std::shared_ptr<const engine::MixedManagedSource> managed){
  auto& s=*impl_;std::lock_guard lock(s.mutex);if(!s.onCaller()||s.closed){return refused("Live mixed Host worker required");}
  if(!managed){return refused("Actual mixed engine seal required");}const auto found=s.prepared.find(managed->identity().originalKey.hex());
  if(found==s.prepared.end()){return {};} // Explicit Public-only continuation; no child means no grant.
  auto result=found->second.child->bindMixedManaged(managed);if(result){found->second.managed=std::move(managed);}return result;
}
std::expected<void,storage::Error> OwnedMixedI2pService::attach(std::shared_ptr<const engine::MixedManagedSource> managed){
  auto& s=*impl_;std::lock_guard lock(s.mutex);if(!s.onCaller()||s.closed||!managed){return refused("Live mixed engine attachment required");}
  const auto found=s.prepared.find(managed->identity().originalKey.hex());
  if(found==s.prepared.end()||found->second.managed!=managed){return refused("No bound mixed source for this actual admission");}
  std::expected<void,Error> result;
  if(!s.onGraph([&]{result=s.graph->add(found->second.child,managed);})){return refused("Mixed I2P event loop unavailable");}return result;
}
std::expected<void,storage::Error> OwnedMixedI2pService::suspend(const core::TorrentHash& hash){
  auto& s=*impl_;std::lock_guard lock(s.mutex);if(!s.onCaller()||s.closed){return refused("Live mixed Host worker required");}
  std::expected<void,Error> result;if(!s.onGraph([&]{result=s.graph->suspend(hash);})){return refused("Mixed I2P event loop unavailable");}return result;
}
std::expected<void,storage::Error> OwnedMixedI2pService::remove(const core::TorrentHash& hash){
  auto& s=*impl_;std::lock_guard lock(s.mutex);if(!s.onCaller()||s.closed){return refused("Live mixed Host worker required");}
  const auto found=s.prepared.find(hash.hex());if(found==s.prepared.end()){s.attempts.erase(hash.hex());return {};}
  std::expected<void,Error> result;if(!s.onGraph([&]{result=s.graph->suspend(hash);if(result){s.graph->routes.erase(hash.hex());}})){return refused("Mixed I2P event loop unavailable");}
  if(!result){return result;}auto released=s.source->releaseMixedChild(hash);if(!released){return released;}s.prepared.erase(found);s.attempts.erase(hash.hex());return {};
}
std::expected<void,storage::Error> OwnedMixedI2pService::publish(const core::TorrentHash& hash,const std::function<std::expected<void,storage::Error>()>& operation){
  auto& s=*impl_;if(!s.onCaller()||s.closed){return refused("Live mixed publication owner required");}return s.source->publish(hash,operation);
}
std::expected<void,storage::Error> OwnedMixedI2pService::publishPositions(const std::function<std::expected<void,storage::Error>()>& operation){
  auto& s=*impl_;if(!s.onCaller()||s.closed){return refused("Live mixed publication owner required");}return s.source->publishPositions(operation);
}
bool OwnedMixedI2pService::sourceCurrent()const noexcept{return impl_->source->prepared();}
void OwnedMixedI2pService::revoke()noexcept{impl_->source->revoke();}
std::vector<OwnedMixedI2pService::Observation> OwnedMixedI2pService::observations()const{
  auto& s=*impl_;std::lock_guard lock(s.mutex);if(!s.onCaller()){return s.last;}
  bool graphFailed=s.closed;std::string graphFailure;std::uint64_t generation=s.last.empty()?0:s.last.front().generation;
  if(!s.closed&&!s.onGraph([&]{s.last=s.graph->observations();graphFailed=s.graph->closing;graphFailure=s.graph->failure;generation=s.graph->generation;})){
    graphFailed=true;graphFailure="Mixed I2P event loop unavailable";
  }
  // A real router failure can precede the first attached route. Keep its
  // requested hash visible without inventing an engine admission or grant.
  for(const auto& [key,attempt]:s.attempts){
    if(std::none_of(s.last.begin(),s.last.end(),[&](const auto& row){return row.hash.hex()==key;})){s.last.push_back(attempt);}
  }
  if(graphFailed){for(auto& row:s.last){row.active=false;row.unavailable=true;row.generation=generation;if(row.failure.empty()){row.failure=graphFailure;}}}
  if(std::any_of(s.last.begin(),s.last.end(),[](const auto& row){return row.unavailable;})){
    // This is an actual filesystem/key comparison, not the revoked bit nor a
    // retired Public admission. No read-only result can restore I2P effects.
    const bool changed=!s.source->validateTransportSnapshot();for(auto& row:s.last){row.sourceChanged=changed;}
  }
  for(auto& row:s.last){row.joined=s.joined;}return s.last;
}
std::expected<void,storage::Error> OwnedMixedI2pService::close(){auto& s=*impl_;std::lock_guard lock(s.mutex);return s.shutdown();}
OwnedMixedI2pService::~OwnedMixedI2pService(){try{auto& s=*impl_;std::lock_guard lock(s.mutex);(void)s.shutdown();}catch(...){}}
}
#else
namespace biglybtc::app {
struct OwnedMixedI2pService::Impl {};
OwnedMixedI2pService::OwnedMixedI2pService(std::unique_ptr<Impl> p):impl_(std::move(p)){}
OwnedMixedI2pService::~OwnedMixedI2pService()=default;
bool OwnedMixedI2pService::available()noexcept{return false;}
std::expected<std::unique_ptr<OwnedMixedI2pService>,storage::Error> OwnedMixedI2pService::create(const storage::ProfileLease&,I2pServiceOptions,std::shared_ptr<engine::MixedI2pTransport>){return std::unexpected(storage::Error{storage::ErrorCode::Unsupported,"Optional mixed I2P transport is unavailable"});}
std::expected<void,storage::Error> OwnedMixedI2pService::prepare(const core::TorrentHash&){return std::unexpected(storage::Error{storage::ErrorCode::Unsupported,"Mixed I2P is unavailable"});}
std::expected<void,storage::Error> OwnedMixedI2pService::bind(std::shared_ptr<const engine::MixedManagedSource>){return std::unexpected(storage::Error{storage::ErrorCode::Unsupported,"Mixed I2P is unavailable"});}
std::expected<void,storage::Error> OwnedMixedI2pService::attach(std::shared_ptr<const engine::MixedManagedSource>){return std::unexpected(storage::Error{storage::ErrorCode::Unsupported,"Mixed I2P is unavailable"});}
std::expected<void,storage::Error> OwnedMixedI2pService::suspend(const core::TorrentHash&){return {};}
std::expected<void,storage::Error> OwnedMixedI2pService::remove(const core::TorrentHash&){return {};}
std::expected<void,storage::Error> OwnedMixedI2pService::publish(const core::TorrentHash&,const std::function<std::expected<void,storage::Error>()>&){return std::unexpected(storage::Error{storage::ErrorCode::Unsupported,"Mixed I2P is unavailable"});}
std::expected<void,storage::Error> OwnedMixedI2pService::publishPositions(const std::function<std::expected<void,storage::Error>()>&){return std::unexpected(storage::Error{storage::ErrorCode::Unsupported,"Mixed I2P is unavailable"});}
bool OwnedMixedI2pService::sourceCurrent()const noexcept{return false;}
void OwnedMixedI2pService::revoke()noexcept{}
std::vector<OwnedMixedI2pService::Observation> OwnedMixedI2pService::observations()const{return {};}
std::expected<void,storage::Error> OwnedMixedI2pService::close(){return {};}
}
std::expected<void,storage::Error> OwnedMixedI2pService::prepareMagnet(const engine::PendingMagnet&){return std::unexpected(storage::Error{storage::ErrorCode::Unsupported,"Mixed metadata unavailable"});}
std::expected<void,storage::Error> OwnedMixedI2pService::bindMetadata(std::shared_ptr<const engine::PendingMetadataSource>){return std::unexpected(storage::Error{storage::ErrorCode::Unsupported,"Mixed metadata unavailable"});}
std::expected<void,storage::Error> OwnedMixedI2pService::attachMetadata(std::shared_ptr<const engine::PendingMetadataSource>){return std::unexpected(storage::Error{storage::ErrorCode::Unsupported,"Mixed metadata unavailable"});}
std::expected<void,storage::Error> OwnedMixedI2pService::removeMetadata(const core::TorrentHash&){return std::unexpected(storage::Error{storage::ErrorCode::Unsupported,"Mixed metadata unavailable"});}
std::expected<void,storage::Error> OwnedMixedI2pService::publishPendingMagnet(const core::TorrentHash&,const std::function<std::expected<void,storage::Error>()>&){return std::unexpected(storage::Error{storage::ErrorCode::Unsupported,"Mixed metadata unavailable"});}
#endif
