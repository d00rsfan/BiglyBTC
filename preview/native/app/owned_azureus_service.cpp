#include "owned_azureus_service.hpp"
#include <QAbstractEventDispatcher>
#include <QCoreApplication>
#include <QEventLoop>
#include <QThread>
#include <algorithm>
#include <array>
#include <condition_variable>
#include <limits>
#include <mutex>
#include <new>
#include <tuple>
#include <utility>

namespace biglybtc::app {
namespace router=services::azureus_router;
namespace nodes=services::azureus_nodes;
struct OwnedAzureusService::Command {
  AzureusServiceCommand kind=AzureusServiceCommand::Ping;
  std::uint64_t id=0,target=0;
  azureus::Peer peer;
  azureus::find::nodes::NodeId node{};
  std::int32_t flags=0,maximumValues=0;
  std::chrono::nanoseconds timeout=std::chrono::seconds(2);
};
namespace {
bool sameCounters(const azureus::Statistics& a,const azureus::Statistics& b){
  const auto fields=[](const auto& v){return std::tie(v.received,v.sent,v.requests,v.replies,v.malformed,v.oversized,v.unowned,v.foreign,v.timeouts,v.cancelled,v.abandoned,v.callbackErrors,v.ioErrors,v.readTurns,v.deliveryTurns,v.serviceCapacityDrops,v.serviceAddressRejected,v.authorityErrors,v.policyErrors);};
  return fields(a)==fields(b);
}
bool sameFacts(const azureus::OwnedDhtStatisticsSnapshot& a,const azureus::OwnedDhtStatisticsSnapshot& b){
  const auto& x=a.transport();const auto& y=b.transport();
  if(x.generation()!=y.generation()||x.network()!=y.network()||x.protocolVersion()!=y.protocolVersion()||x.family()!=y.family()||x.state()!=y.state()
      ||x.pending()!=y.pending()||x.completions()!=y.completions()||x.allowedPeers()!=y.allowedPeers()||!sameCounters(x.counters(),y.counters())
      ||a.operationsEnabled()!=b.operationsEnabled()||a.vivaldiEnabled()!=b.vivaldiEnabled()||a.droppedEvents()!=b.droppedEvents()
      ||a.localPosition()!=b.localPosition()||a.remotePosition()!=b.remotePosition()||a.lastRttNanoseconds()!=b.lastRttNanoseconds()
      ||a.coordinateObservations()!=b.coordinateObservations()||a.events().size()!=b.events().size()){return false;}
  for(std::size_t i=0;i<a.events().size();++i){const auto& p=a.events()[i];const auto& q=b.events()[i];
    if(std::tie(p.serial,p.ticket,p.requestAction,p.kind,p.elapsedNanoseconds)!=std::tie(q.serial,q.ticket,q.requestAction,q.kind,q.elapsedNanoseconds)){return false;}}
  return true; // Pull publication itself is deliberately excluded from fact equality.
}
bool validPeer(const azureus::Peer& peer,const QHostAddress& bound){return peer.port&&peer.address.isLoopback()&&peer.address.scopeId().isEmpty()&&peer.address.protocol()==bound.protocol();}
bool validOptions(const AzureusServiceOptions& value){return value.bindAddress.isLoopback()&&value.bindAddress.scopeId().isEmpty()
    &&(value.bindAddress.protocol()==QAbstractSocket::IPv4Protocol||value.bindAddress.protocol()==QAbstractSocket::IPv6Protocol)
    &&value.network>=0&&value.version>=51&&value.version<=55&&value.maximumPending&&value.maximumPending<=32
    &&value.maximumPeers&&value.maximumPeers<=32&&value.maximumCommands&&value.maximumCommands<=64;}
}
struct OwnedAzureusService::Impl {
  struct State {
    mutable std::mutex mutex;
    std::condition_variable readyCondition;
    QObject* owner=nullptr;
    QAbstractEventDispatcher* dispatcher=nullptr;
    azureus::Peer endpoint;
    AzureusServiceObservation value;
    bool ready=false,stop=false;
    std::uint64_t nextRequest=0;
    void advance(){if(value.sequence!=std::numeric_limits<std::uint64_t>::max()){++value.sequence;}else{value.current=false;value.error=AzureusServiceError::Capacity;}}
  };
  class Owner final:public QObject {
  public:
    Owner(std::shared_ptr<State> state,const AzureusServiceOptions& options,QEventLoop& loop)
        :state_(std::move(state)),options_(options),loop_(loop),actor_(configuration(options)){}
    std::expected<void,azureus::Failure> start(){
      auto started=actor_.start(options_.bindAddress);if(started){initializeRouter();}return started;
    }
    azureus::Peer endpoint()const{return actor_.endpoint();}
    void idle(){bool stop=false;{std::lock_guard lock(state_->mutex);stop=state_->stop;}if(stop){loop_.quit();return;}publish();}
    void command(Command value){
      {std::lock_guard lock(state_->mutex);if(state_->stop){releaseLocked();state_->advance();return;}}
      try{
        if(value.kind==AzureusServiceCommand::AllowPeer){auto allowed=actor_.allowPeer(value.peer);if(allowed){routerKnown(value.peer);}complete(value,allowed);return;}
        if(value.kind==AzureusServiceCommand::Cancel){
          for(auto& item:active_){if(item.id==value.target){const bool cancelled=actor_.cancel(item.ticket);complete(value,cancelled?std::expected<void,azureus::Failure>{}:std::unexpected(azureus::Failure::State));return;}}
          complete(value,std::unexpected(azureus::Failure::State));return;
        }
        auto allowed=actor_.allowPeer(value.peer);if(!allowed){complete(value,allowed);return;}routerKnown(value.peer);
        Active* slot=nullptr;for(auto& item:active_){if(!item.id){slot=&item;break;}}
        if(!slot){complete(value,std::unexpected(azureus::Failure::Capacity));return;}
        slot->id=value.id;
        std::expected<azureus::Ticket,azureus::Failure> submitted=std::unexpected(azureus::Failure::State);
        if(value.kind==AzureusServiceCommand::Ping){submitted=actor_.ping(value.peer,options_.version,this,[this,value](azureus::Outcome result){routerReply(value.peer,result);reply(value,result?std::optional<azureus::Failure>{}:result.error(),true);},value.timeout);}
        else if(value.kind==AzureusServiceCommand::FindNode){submitted=actor_.findNode(value.peer,options_.version,value.node,this,[this,value](azureus::FindNodeOutcome result){routerReply(value.peer,result);reply(value,result?std::optional<azureus::Failure>{}:result.error(),true);},value.timeout);}
        else if(value.kind==AzureusServiceCommand::FindValue){submitted=actor_.findValue(value.peer,options_.version,value.node,value.flags,value.maximumValues,this,[this,value](azureus::FindValueOutcome result){routerReply(value.peer,result);const auto* body=result?std::get_if<azureus::lookup::ReplyBody>(&result->body()):nullptr;const bool terminal=!body||!body->continuation;reply(value,result?std::optional<azureus::Failure>{}:result.error(),terminal);},value.timeout);}
        if(!submitted){slot->id=0;complete(value,std::unexpected(submitted.error()));return;}slot->ticket=*submitted;publish();
      }catch(...){for(auto& slot:active_){if(slot.id==value.id){slot.id=0;}}failCommand(value,AzureusServiceError::Memory);{std::lock_guard lock(state_->mutex);state_->stop=true;state_->value.current=false;state_->value.error=AzureusServiceError::Memory;state_->value.state=AzureusServiceState::Closing;state_->advance();}loop_.quit();}
    }
    void shutdown()noexcept{
      bool closed=true;try{actor_.close();}catch(...){closed=false;}
      if(closed){publish();}
      std::lock_guard lock(state_->mutex);state_->value.state=closed?AzureusServiceState::Closed:AzureusServiceState::Failed;
      if(!closed){state_->value.current=false;state_->value.error=AzureusServiceError::Memory;}
      state_->value.current=false;state_->value.outstandingCommands=0;state_->advance();
    }
  private:
    struct Active {std::uint64_t id=0;azureus::Ticket ticket;};
    using Clock=std::chrono::steady_clock;
    std::int64_t routerNow()const noexcept{
      return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now()-routerStarted_).count();
    }
    // NodeCache implements the pinned source's protocol-dependent numeric-ID
    // derivation. Different endpoints can legitimately share a restricted ID.
    std::expected<nodes::NodeId,router::RouterError> routerIdentity(const azureus::Peer& peer,std::int8_t version){
      nodes::ResolvedEndpoint input;input.address.port=peer.port;
      if(peer.address.protocol()==QAbstractSocket::IPv4Protocol){
        const auto value=peer.address.toIPv4Address();input.address.length=4;
        for(unsigned i=0;i<4;++i){input.address.bytes[i]=static_cast<std::uint8_t>(value>>(24-8*i));}
      }else if(peer.address.protocol()==QAbstractSocket::IPv6Protocol){
        input.address.length=16;const auto value=peer.address.toIPv6Address();
        std::copy_n(value.c,16,input.address.bytes.begin());
      }else{return std::unexpected(router::RouterError::Invalid);}
      const auto result=nodeCache_.lookup(input,version);
      if(!result){return std::unexpected(router::RouterError::Invalid);}return result->id;
    }
    void routerFailed(router::RouterError error)noexcept{
      router_.reset();routerSnapshot_.reset();routerFailure_=error;
    }
    void initializeRouter()noexcept{
      try{
        routerStarted_=Clock::now();const auto id=routerIdentity(actor_.endpoint(),options_.version);
        if(!id){routerFailed(id.error());return;}
        const auto transport=actor_.statisticsSnapshot();
        if(!transport){routerFailed(router::RouterError::Invalid);return;}
        auto made=router::RouterOwner::create(*id,{},transport->generation(),0);
        if(!made){routerFailed(made.error());return;}router_=std::move(*made);
      }catch(...){routerFailed(router::RouterError::Memory);}
    }
    void routerKnown(const azureus::Peer& peer)noexcept{
      if(!router_){return;}
      try{
        const auto id=routerIdentity(peer,options_.version);if(!id){routerFailed(id.error());return;}
        const auto result=router_->contactKnown(router::ContactInput{.id=*id},routerNow());
        if(!result){routerFailed(result.error());}
      }catch(...){routerFailed(router::RouterError::Memory);}
    }
    template<class Result>void routerReply(const azureus::Peer& peer,const Result& result)noexcept{
      if(!router_){return;}
      try{
        // Only the transport's already endpoint/transaction/connection/action-
        // matched decoded response proves this explicit peer responded. This is
        // transport correlation, not cryptographic remote identity proof.
        if(result){
          const auto& header=result->header().fields();const auto id=routerIdentity(peer,header.version);
          if(!id){routerFailed(id.error());return;}
          router::ContactInput contact{.id=*id,.instance=header.instance};
          contact.sleeping=(static_cast<std::uint8_t>(header.flags)&1U)!=0; // GF_DHT_SLEEPING.
          const auto alive=router_->contactAlive(contact,routerNow());if(!alive){routerFailed(alive.error());}
        }else if(result.error()==azureus::Failure::Timeout){
          // Admission/send/cancellation/close/remote-error callbacks do not
          // establish peer death. Only an actual outstanding-request timeout
          // advances the configured router failure threshold here.
          const auto id=routerIdentity(peer,options_.version);if(!id){routerFailed(id.error());return;}
          const auto dead=router_->contactDead(*id,routerNow());if(!dead){routerFailed(dead.error());}
        }
      }catch(...){routerFailed(router::RouterError::Memory);}
    }
    void captureRouter()noexcept{
      if(!router_){return;}
      try{
        const auto actual=router_->snapshot();if(!actual){routerFailed(actual.error());return;}
        if(routerSnapshot_&&routerSnapshot_->revision()==actual->revision()){return;}
        routerSnapshot_=std::make_shared<const router::RouterStatisticsSnapshot>(*actual);
      }catch(...){routerFailed(router::RouterError::Memory);}
    }

    static azureus::Config configuration(const AzureusServiceOptions& options){azureus::Config config;config.network=options.network;config.version=options.version;
      config.servePings=options.servePings;config.observeOperations=true;config.enableVivaldi=options.enableVivaldi;config.maximumPending=options.maximumPending;config.maximumPeers=options.maximumPeers;return config;}
    void releaseLocked(){if(state_->value.outstandingCommands){--state_->value.outstandingCommands;}}
    void reply(const Command& value,std::optional<azureus::Failure> failure,bool terminal){
      if(terminal){for(auto& slot:active_){if(slot.id==value.id){slot.id=0;break;}}}
      publish();{std::lock_guard lock(state_->mutex);if(terminal){releaseLocked();}state_->value.lastResult=AzureusServiceResult{value.id,value.target,value.kind,terminal,failure?std::optional{AzureusServiceError::Transport}:std::nullopt,failure};state_->advance();}
    }
    void complete(const Command& value,std::expected<void,azureus::Failure> result){reply(value,result?std::optional<azureus::Failure>{}:result.error(),true);}
    void failCommand(const Command& value,AzureusServiceError error){
      {std::lock_guard lock(state_->mutex);releaseLocked();state_->value.lastResult=AzureusServiceResult{value.id,value.target,value.kind,true,error,{}};state_->advance();}publish();
    }
    void publish()noexcept{
      try{
        const auto actual=actor_.dhtStatisticsSnapshot(); // Exported getter is noexcept and fixed-state.
        if(!actual){std::lock_guard lock(state_->mutex);state_->value.current=false;state_->value.error=AzureusServiceError::Transport;state_->value.transportFailure=actual.error();state_->advance();return;}
        captureRouter();
        if(last_&&sameFacts(*last_,*actual)){std::lock_guard lock(state_->mutex);const bool current=!state_->stop&&state_->value.state==AzureusServiceState::Running&&actual->transport().state()==azureus::TransportStatisticsState::Bound;
          if(state_->value.current!=current||state_->value.error||state_->value.routerSnapshot!=routerSnapshot_||state_->value.routerFailure!=routerFailure_){
            state_->value.current=current;state_->value.error.reset();state_->value.transportFailure.reset();
            state_->value.routerSnapshot=routerSnapshot_;state_->value.routerFailure=routerFailure_;state_->advance();}return;}
        auto copied=std::make_shared<const azureus::OwnedDhtStatisticsSnapshot>(*actual);
        std::lock_guard lock(state_->mutex);last_=copied;state_->value.snapshot=std::move(copied);state_->value.routerSnapshot=routerSnapshot_;state_->value.routerFailure=routerFailure_;state_->value.current=!state_->stop&&state_->value.state==AzureusServiceState::Running&&actual->transport().state()==azureus::TransportStatisticsState::Bound;state_->value.error.reset();state_->value.transportFailure.reset();state_->advance();
      }catch(...){std::lock_guard lock(state_->mutex);state_->value.current=false;state_->value.error=AzureusServiceError::Memory;state_->advance();}
    }
    std::shared_ptr<State> state_;
    AzureusServiceOptions options_;
    QEventLoop& loop_;
    azureus::OwnedPingTransport actor_;
    std::array<Active,32> active_{};
    std::shared_ptr<const azureus::OwnedDhtStatisticsSnapshot> last_;
    nodes::NodeCache nodeCache_; // Constructed, used and destroyed on this owner.
    Clock::time_point routerStarted_{};
    std::unique_ptr<router::RouterOwner> router_;
    std::shared_ptr<const router::RouterStatisticsSnapshot> routerSnapshot_;
    std::optional<router::RouterError> routerFailure_;
  };
  class Thread final:public QThread {
  public:
    Thread(std::shared_ptr<State> state,AzureusServiceOptions options):state_(std::move(state)),options_(std::move(options)){}
  protected:
    void run()override{
      try{
        QEventLoop loop;Owner owner(state_,options_,loop);struct Unbind{std::shared_ptr<State> state;~Unbind(){std::lock_guard lock(state->mutex);state->owner=nullptr;state->dispatcher=nullptr;}} unbind{state_};auto* dispatcher=QAbstractEventDispatcher::instance();const auto started=owner.start();
        if(!started||!dispatcher){failure(AzureusServiceError::Start,started?std::optional<azureus::Failure>{}:started.error());return;}
        const auto connection=QObject::connect(dispatcher,&QAbstractEventDispatcher::aboutToBlock,&owner,[&owner]{owner.idle();},Qt::DirectConnection);
        {std::lock_guard lock(state_->mutex);state_->owner=&owner;state_->dispatcher=dispatcher;state_->endpoint=owner.endpoint();state_->value.state=AzureusServiceState::Running;state_->advance();}
        owner.idle();{std::lock_guard lock(state_->mutex);state_->ready=true;}state_->readyCondition.notify_all();bool stop=false;{std::lock_guard lock(state_->mutex);stop=state_->stop;}if(!stop){loop.exec();}
        QObject::disconnect(connection);owner.shutdown();
        {std::lock_guard lock(state_->mutex);state_->owner=nullptr;state_->dispatcher=nullptr;}
      }catch(...){failure(AzureusServiceError::Memory,{});}
    }
  private:
    void failure(AzureusServiceError error,std::optional<azureus::Failure> transport){
      {std::lock_guard lock(state_->mutex);state_->owner=nullptr;state_->dispatcher=nullptr;state_->value.state=AzureusServiceState::Failed;state_->value.current=false;state_->value.error=error;state_->value.transportFailure=transport;state_->value.outstandingCommands=0;state_->ready=true;state_->advance();}state_->readyCondition.notify_all();
    }
    std::shared_ptr<State> state_;AzureusServiceOptions options_;
  };
  explicit Impl(AzureusServiceOptions options):options(std::move(options)),thread(state,this->options){}
  std::shared_ptr<State> state=std::make_shared<State>();
  AzureusServiceOptions options;
  Thread thread;
  std::mutex closeMutex;
};
OwnedAzureusService::OwnedAzureusService(std::unique_ptr<Impl> impl):impl_(std::move(impl)){}
std::expected<std::unique_ptr<OwnedAzureusService>,AzureusServiceError> OwnedAzureusService::create(AzureusServiceOptions options){
  if(!options.enabled){return std::unexpected(AzureusServiceError::Disabled);}if(!validOptions(options)){return std::unexpected(AzureusServiceError::Invalid);}
  if(!QCoreApplication::instance()||QCoreApplication::closingDown()){return std::unexpected(AzureusServiceError::NoApplication);}
  try{
    auto service=std::unique_ptr<OwnedAzureusService>(new OwnedAzureusService(std::make_unique<Impl>(std::move(options))));service->impl_->thread.start();
    auto& state=*service->impl_->state;std::unique_lock lock(state.mutex);
    const bool ready=state.readyCondition.wait_for(lock,std::chrono::seconds(5),[&]{return state.ready;});
    const auto error=!ready?std::optional{AzureusServiceError::Start}:state.value.state==AzureusServiceState::Failed?state.value.error:std::nullopt;lock.unlock();
    if(error){(void)service->close();return std::unexpected(*error);}return service;
  }catch(...){return std::unexpected(AzureusServiceError::Memory);}
}
OwnedAzureusService::~OwnedAzureusService(){(void)close();}
azureus::Peer OwnedAzureusService::endpoint()const{std::lock_guard lock(impl_->state->mutex);return impl_->state->endpoint;}
AzureusServiceObservation OwnedAzureusService::observation()const{std::lock_guard lock(impl_->state->mutex);return impl_->state->value;}
std::expected<std::uint64_t,AzureusServiceError> OwnedAzureusService::submit(Command value){
  auto& state=*impl_->state;std::lock_guard lock(state.mutex);
  if(state.stop||state.value.state!=AzureusServiceState::Running||!state.owner){return std::unexpected(AzureusServiceError::Closed);}
  if(value.kind!=AzureusServiceCommand::Cancel&&!validPeer(value.peer,state.endpoint.address)){return std::unexpected(AzureusServiceError::Invalid);}
  if(value.kind==AzureusServiceCommand::Cancel&&!value.target){return std::unexpected(AzureusServiceError::Invalid);}
  if(value.kind!=AzureusServiceCommand::AllowPeer&&value.kind!=AzureusServiceCommand::Cancel&&(value.timeout<=std::chrono::nanoseconds::zero()||value.timeout>std::chrono::seconds(30))){return std::unexpected(AzureusServiceError::Invalid);}
  if(value.kind==AzureusServiceCommand::FindValue&&(value.maximumValues<0||value.maximumValues>255)){return std::unexpected(AzureusServiceError::Invalid);}
  if(state.value.outstandingCommands>=impl_->options.maximumCommands||state.nextRequest==std::numeric_limits<std::uint64_t>::max()){return std::unexpected(AzureusServiceError::Capacity);}
  value.id=++state.nextRequest;++state.value.outstandingCommands;auto* owner=static_cast<Impl::Owner*>(state.owner);
  try{if(!QMetaObject::invokeMethod(owner,[owner,value]{owner->command(value);},Qt::QueuedConnection)){--state.value.outstandingCommands;return std::unexpected(AzureusServiceError::Dispatch);}}
  catch(...){--state.value.outstandingCommands;return std::unexpected(AzureusServiceError::Memory);}state.advance();return value.id;
}
std::expected<std::uint64_t,AzureusServiceError> OwnedAzureusService::allowPeer(azureus::Peer peer){Command value;value.kind=AzureusServiceCommand::AllowPeer;value.peer=std::move(peer);return submit(value);}
std::expected<std::uint64_t,AzureusServiceError> OwnedAzureusService::ping(azureus::Peer peer,std::chrono::nanoseconds timeout){Command value;value.peer=std::move(peer);value.timeout=timeout;return submit(value);}
std::expected<std::uint64_t,AzureusServiceError> OwnedAzureusService::findNode(azureus::Peer peer,azureus::find::nodes::NodeId target,std::chrono::nanoseconds timeout){Command value;value.kind=AzureusServiceCommand::FindNode;value.peer=std::move(peer);value.node=target;value.timeout=timeout;return submit(value);}
std::expected<std::uint64_t,AzureusServiceError> OwnedAzureusService::findValue(azureus::Peer peer,azureus::lookup::nodes::NodeId target,std::int32_t flags,std::int32_t maximumValues,std::chrono::nanoseconds timeout){Command value;value.kind=AzureusServiceCommand::FindValue;value.peer=std::move(peer);value.node=target;value.flags=flags;value.maximumValues=maximumValues;value.timeout=timeout;return submit(value);}
std::expected<std::uint64_t,AzureusServiceError> OwnedAzureusService::cancel(std::uint64_t request){Command value;value.kind=AzureusServiceCommand::Cancel;value.target=request;return submit(value);}
std::expected<void,AzureusServiceError> OwnedAzureusService::close(){
  if(QThread::currentThread()==&impl_->thread){return std::unexpected(AzureusServiceError::Thread);}std::lock_guard lifecycle(impl_->closeMutex);auto& state=*impl_->state;
  {std::lock_guard lock(state.mutex);if(state.value.joined){return {}; }state.stop=true;state.value.current=false;if(state.value.state==AzureusServiceState::Running){state.value.state=AzureusServiceState::Closing;state.advance();}if(state.dispatcher){state.dispatcher->wakeUp();}}
  if(!impl_->thread.wait()){return std::unexpected(AzureusServiceError::Thread);}
  std::lock_guard lock(state.mutex);state.value.joined=true;state.advance();return {};
}
} // namespace biglybtc::app
