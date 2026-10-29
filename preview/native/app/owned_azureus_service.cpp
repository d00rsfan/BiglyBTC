#include "owned_azureus_service.hpp"
#include "azureus_profile_io.hpp"
#include <QScopeGuard>
#include <QAbstractEventDispatcher>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QEventLoop>
#include <QThread>
#include <algorithm>
#include <array>
#include <condition_variable>
#include <deque>
#include <limits>
#include <mutex>
#include <new>
#include <tuple>
#include <utility>

namespace biglybtc::app {
namespace router=services::azureus_router;
namespace nodes=services::azureus_nodes;
namespace control=services::azureus_control;
struct OwnedAzureusService::Command {
  AzureusServiceCommand kind=AzureusServiceCommand::Ping;
  std::uint64_t id=0,target=0;
  azureus::Peer peer;
  azureus::find::nodes::NodeId node{};
  std::int32_t flags=0,maximumValues=0;
  std::chrono::nanoseconds timeout=std::chrono::seconds(2);
  std::chrono::steady_clock::time_point deadline{};
  std::string applicationKey,payload;
  AzureusPublicationFlags publicationFlags=AzureusPublicationFlags::Normal;
  std::optional<core::TorrentHash> ratingHash;
  std::array<std::uint8_t,32> ratingRevision{};
  std::shared_ptr<const storage::DistributedRatingSnapshot> ratingSubject;
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
  if(a.pingObservationsLost()!=b.pingObservationsLost()||bool(a.pingObservation())!=bool(b.pingObservation())){return false;}
  if(a.pingObservation()){
    const auto& p=*a.pingObservation();const auto& q=*b.pingObservation();
    if(p.generation()!=q.generation()||p.sequence()!=q.sequence()||p.kind()!=q.kind()||p.endpoint()!=q.endpoint()
        ||p.observedSteadyNanoseconds()!=q.observedSteadyNanoseconds()||p.rttNanoseconds()!=q.rttNanoseconds()){return false;}
  }
  for(std::size_t i=0;i<a.events().size();++i){const auto& p=a.events()[i];const auto& q=b.events()[i];
    if(std::tie(p.serial,p.ticket,p.requestAction,p.kind,p.elapsedNanoseconds)!=std::tie(q.serial,q.ticket,q.requestAction,q.kind,q.elapsedNanoseconds)){return false;}}
  return true; // Pull publication itself is deliberately excluded from fact equality.
}
bool validPeer(const azureus::Peer& peer,const QHostAddress& bound){return peer.port&&peer.address.isLoopback()&&peer.address.scopeId().isEmpty()&&peer.address.protocol()==bound.protocol();}
bool validOptions(const AzureusServiceOptions& value){return value.bindAddress.isLoopback()&&value.bindAddress.scopeId().isEmpty()
    &&(value.bindAddress.protocol()==QAbstractSocket::IPv4Protocol||value.bindAddress.protocol()==QAbstractSocket::IPv6Protocol)
    &&value.network>=0&&value.version>=51&&value.version<=55&&value.maximumPending&&value.maximumPending<=32
    &&value.maximumPeers&&value.maximumPeers<=32&&value.maximumCommands&&value.maximumCommands<=64
    &&(!value.contacts||(value.valueVersions&&value.contacts->network()==value.network&&value.contacts->family()==(value.bindAddress.protocol()==QAbstractSocket::IPv4Protocol?4:16)&&value.contacts->profileOwnership()->sharesOwnership(*value.valueVersions->profileOwnership())))
    &&(!value.maintenance||(value.contacts&&value.routingScope==azureus::RoutingScope::OwnedLoopback&&value.serveValues&&value.valueVersions))
    &&(!value.valueVersions||value.valueVersions->network()==value.network)&&(!value.serveValues||value.valueVersions)
    &&(value.routingScope==azureus::RoutingScope::ExactPeers||(value.routingScope==azureus::RoutingScope::OwnedLoopback&&value.serveValues&&value.valueVersions));}
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
    std::shared_ptr<detail::AzureusProfileIo> profileIo;
    std::uint64_t ratingRevision=0;
    std::uint64_t nextRequest=0;
    void advance(){if(value.sequence!=std::numeric_limits<std::uint64_t>::max()){++value.sequence;}else{value.current=false;value.error=AzureusServiceError::Capacity;}}
  };
  class Owner final:public QObject {
  public:
    Owner(std::shared_ptr<State> state,const AzureusServiceOptions& options,QEventLoop& loop)
        :state_(std::move(state)),options_(options),loop_(loop),actor_(configuration(options)){
      maintenanceTimer_.setTimerType(Qt::PreciseTimer);maintenanceTimer_.setInterval(std::chrono::seconds(10));QObject::connect(&maintenanceTimer_,&QChronoTimer::timeout,this,[this]{try{maintenanceWake();}catch(...){publicationMemoryFailure();}});
      ioDeadline_.setSingleShot(true);QObject::connect(&ioDeadline_,&QChronoTimer::timeout,this,[this]{drainProfileIo();});
    }
    bool event(QEvent* value)override{
      if(state_->profileIo&&value->type()==ProfileIo::wakeEvent()){if(state_->profileIo){state_->profileIo->drainWake();}drainProfileIo();return true;}
      return QObject::event(value);
    }
    std::expected<void,azureus::Failure> start(){
      if(state_->profileIo){state_->profileIo->bind(this);}
      auto started=actor_.start(options_.bindAddress,options_.bindPort);if(!started){return started;}initializeRouter();
      if(options_.valueVersions&&!initializeLocalDatabase()){return std::unexpected(azureus::Failure::State);}
      if(options_.serveValues){
        if(!router_||!localDatabase_){return std::unexpected(azureus::Failure::State);}
        profilePermit_=true;const auto bound=actor_.serveDatabase(router_,localDatabase_,[this]{
          {std::lock_guard lock(state_->mutex);if(state_->stop){return false;}}
          return bool(router_)&&profilePermit_;
        },[this](std::move_only_function<void(bool)> complete){return normalProfileRequest(std::move(complete));});profilePermit_=false;if(!bound){return bound;}
      }if(options_.maintenance){maintenanceTimer_.start();}return started;
    }
    azureus::Peer endpoint()const{return actor_.endpoint();}
    void idle(){drainProfileIo();bool stop=false;{std::lock_guard lock(state_->mutex);stop=state_->stop;}if(stop){loop_.quit();return;}publish();}
    void command(Command value){
      if(value.deadline==std::chrono::steady_clock::time_point{}){value.deadline=std::chrono::steady_clock::now()+value.timeout;}
      {std::lock_guard lock(state_->mutex);if(state_->stop){releaseLocked();state_->advance();return;}}
      try{
        if(value.kind==AzureusServiceCommand::AllowPeer){auto allowed=actor_.allowPeer(value.peer);if(allowed){rememberOwned(value.peer);routerKnown(value.peer);}complete(value,allowed);return;}
        if(value.kind==AzureusServiceCommand::LookupValue){beginLookup(value);return;}
        if(value.kind==AzureusServiceCommand::PublishValue||value.kind==AzureusServiceCommand::ForwardCached){beginPublish(value);return;}
        if(value.kind==AzureusServiceCommand::IntegrateRouting){beginRouting(value);return;}
        if(value.kind==AzureusServiceCommand::BootstrapSaved){beginSavedRouting(value);return;}
        if(value.kind==AzureusServiceCommand::FlushContacts){beginContactFlush(value);return;}
        if(value.kind==AzureusServiceCommand::PublishRating||value.kind==AzureusServiceCommand::ReadRatings){beginRating(std::move(value));return;}
        if(value.kind==AzureusServiceCommand::Cancel){
          if(routingCommand_&&routingCommand_->id==value.target&&!routingResultPublished_){
            if(routingOperation_){const bool accepted=actor_.cancelRouting(routingOperation_);captureOwnedRouting();complete(value,accepted?std::expected<void,azureus::Failure>{}:std::unexpected(azureus::Failure::State));}
            else{routingResultPublished_=true;reply(*routingCommand_,azureus::Failure::Cancelled,true);drainProfileIo();complete(value,{});}return;
          }
          if(ratingRead_&&ratingRead_->command.id==value.target&&!ratingRead_->finished){
            ratingRead_->cancelled=true;const bool cancelled=actor_.cancel(ratingRead_->ticket)||bool(state_->profileIo);finishRatingRead(ratingRead_,AzureusRatingReadState::Cancelled,AzureusServiceError::Transport,azureus::Failure::Cancelled);drainProfileIo();
            complete(value,cancelled?std::expected<void,azureus::Failure>{}:std::unexpected(azureus::Failure::State));return;
          }
          if(publishing_&&publishing_->command.id==value.target&&!publishing_->finished){
            publishing_->cancelled=true;const bool cancelled=actor_.cancel(publishing_->ticket)||bool(state_->profileIo);finishPublish(publishing_,AzureusPublishState::Cancelled,AzureusServiceError::Transport,azureus::Failure::Cancelled);drainProfileIo();
            complete(value,cancelled?std::expected<void,azureus::Failure>{}:std::unexpected(azureus::Failure::State));return;
          }
          for(auto& pending:ioSlots_){
            if(pending.job&&pending.command==value.target&&!pending.consumed&&!pending.expiryNotified&&pending.cancelled){
              ProfileIo::revoke(pending.job);auto cancelled=std::move(pending.cancelled);cancelled();drainProfileIo();complete(value,{});return;
            }
          }
          if(lookupCommand_&&lookupCommand_->id==value.target&&!lookupResultPublished_){
            const auto cancelled=lookup_->cancel(lookupNow());pumpLookup();
            complete(value,cancelled?std::expected<void,azureus::Failure>{}:std::unexpected(azureus::Failure::State));return;
          }
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
      ioDeadline_.stop();maintenanceTimer_.stop();
      bool closed=true;try{actor_.close();}catch(...){closed=false;}
      if(state_->profileIo){state_->profileIo->requestStop();}
      if(closed){
        if(publishing_&&!publishing_->finished){try{finishPublish(publishing_,AzureusPublishState::Closed,AzureusServiceError::Closed,azureus::Failure::Closed);}catch(...){publicationMemoryFailure();}}
        if(ratingRead_&&!ratingRead_->finished){try{finishRatingRead(ratingRead_,AzureusRatingReadState::Closed,AzureusServiceError::Closed,azureus::Failure::Closed);}catch(...){publicationMemoryFailure();}}
        if(localDatabase_){localDatabase_->close();}
        // actor.close() actually closes all outstanding tickets. Its queued
        // callbacks need not run after the event loop stops; record only that
        // actual Closed boundary, never successful child replies.
        if(lookup_&&!lookupResultPublished_){
          static_cast<void>(lookup_->close(lookupNow()));
          for(auto& rpc:lookupRpcs_){if(rpc.node){static_cast<void>(lookup_->finish(rpc.node,control::LookupNodeState::Closed,lookupNow()));rpc={};}}
          finishLookup();
        }
        publish();
      }
      std::lock_guard lock(state_->mutex);state_->value.state=closed?AzureusServiceState::Closed:AzureusServiceState::Failed;
      if(!closed){state_->value.current=false;state_->value.error=AzureusServiceError::Memory;}
      state_->value.current=false;state_->value.outstandingCommands=0;state_->advance();
    }
  private:
    struct Active {std::uint64_t id=0;azureus::Ticket ticket;};
    #include "azureus_profile_io_owner.inc"
    #include "azureus_local_publish.inc"
    #include "azureus_rating_consumer.inc"
    #include "azureus_routing_owner.inc"
    #include "azureus_maintenance_owner.inc"

    struct LookupRpc {std::uint64_t node=0;azureus::Ticket ticket;azureus::Peer peer;bool cancellationRequested=false;};
    using Clock=std::chrono::steady_clock;
    std::int64_t routerNow()const noexcept{
      const auto now=Clock::now();return std::chrono::duration_cast<std::chrono::milliseconds>(options_.serveValues?now.time_since_epoch():now-routerStarted_).count();
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
        auto made=router::RouterOwner::create(*id,{},transport->generation(),routerNow(),QDateTime::currentMSecsSinceEpoch());
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
          const auto alive=router_->contactAlive(contact,routerNow(),QDateTime::currentMSecsSinceEpoch());if(!alive){routerFailed(alive.error());}
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

    static std::int64_t lookupNow()noexcept{return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count();}
    static std::optional<azureus::Peer> lookupPeer(const services::azureus_dht::Address& value){
      if(!value.port){return {};}
      if(value.length==4){quint32 address=0;for(unsigned i=0;i<4;++i){address=(address<<8)|value.bytes[i];}return azureus::Peer{QHostAddress(address),value.port};}
      if(value.length==16){Q_IPV6ADDR address;std::copy_n(value.bytes.begin(),16,address.c);return azureus::Peer{QHostAddress(address),value.port};}
      return {};
    }
    bool isOwned(const azureus::Peer& peer)const noexcept{
      return actor_.isRoutingPeer(peer)||std::ranges::any_of(ownedPeers_,[&](const auto& item){return item&&*item==peer;});
    }
    void rememberOwned(const azureus::Peer& peer)noexcept{
      if(isOwned(peer)){return;}for(auto& item:ownedPeers_){if(!item){item=peer;return;}}
    }
    control::AdmittedContact lookupAdmission(const nodes::Contact& contact)const{
      const auto peer=lookupPeer(contact.address);
      return {contact,peer&&validPeer(*peer,options_.bindAddress)&&isOwned(*peer)};
    }
    void failLookup(AzureusServiceError error)noexcept{
      // Optional lookup failure never stops the actor, changes byte accounting
      // or promotes an outstanding request to successful completion.
      for(auto& rpc:lookupRpcs_){if(rpc.node){try{static_cast<void>(actor_.cancel(rpc.ticket));}catch(...){}rpc={};}}
      lookup_.reset();lookupSnapshot_.reset();lookupFailure_=error;
      if(lookupCommand_&&!lookupResultPublished_){
        lookupResultPublished_=true;
        std::lock_guard lock(state_->mutex);releaseLocked();
        state_->value.lastResult=AzureusServiceResult{lookupCommand_->id,0,AzureusServiceCommand::LookupValue,true,error,{}};state_->advance();
      }
      publish();
    }
    void captureLookup()noexcept{
      if(!lookup_){return;}
      try{
        auto actual=lookup_->snapshot();if(!actual){lookupSnapshot_.reset();lookupFailure_=AzureusServiceError::Memory;return;}
        if(lookupSnapshot_&&lookupSnapshot_->lookup==actual->lookup&&lookupSnapshot_->revision==actual->revision){return;}
        lookupSnapshot_=std::make_shared<const control::LookupSnapshot>(std::move(*actual));lookupFailure_.reset();
      }catch(...){lookupSnapshot_.reset();lookupFailure_=AzureusServiceError::Memory;}
    }
    void finishLookup()noexcept{
      if(!lookup_||!lookupCommand_||lookupResultPublished_){return;}
      try{
        auto actual=lookup_->snapshot();if(!actual){failLookup(AzureusServiceError::Memory);return;}
        if(!control::lookupTerminal(actual->state)||std::ranges::any_of(lookupRpcs_,[](const auto& rpc){return rpc.node!=0;})){return;}
        std::optional<azureus::Failure> failure;
        switch(actual->state){
          case control::LookupState::Complete:break;
          case control::LookupState::Cancelled:failure=azureus::Failure::Cancelled;break;
          case control::LookupState::Closed:failure=azureus::Failure::Closed;break;
          case control::LookupState::TimedOut:failure=azureus::Failure::Timeout;break;
          default:failure=azureus::Failure::State;break;
        }
        lookupResultPublished_=true;reply(*lookupCommand_,failure,true);
      }catch(...){failLookup(AzureusServiceError::Memory);}
    }
    void lookupReply(std::uint64_t operation,std::uint64_t node,azureus::FindValueOutcome outcome)noexcept{
      if(!lookup_||!lookupCommand_||lookupCommand_->id!=operation||lookupResultPublished_){return;}
      auto found=std::ranges::find(lookupRpcs_,node,&LookupRpc::node);if(found==lookupRpcs_.end()){return;}
      try{
        const auto now=lookupNow();bool terminal=true;
        const auto peer=found->peer;routerReply(peer,outcome);
        if(!outcome){
          auto state=control::LookupNodeState::Failed;
          if(outcome.error()==azureus::Failure::Timeout){state=control::LookupNodeState::Timeout;}
          else if(outcome.error()==azureus::Failure::Cancelled){state=control::LookupNodeState::Cancelled;}
          else if(outcome.error()==azureus::Failure::Closed){state=control::LookupNodeState::Closed;}
          static_cast<void>(lookup_->finish(node,state,now));
        }else if(const auto* body=std::get_if<azureus::lookup::ReplyBody>(&outcome->body())){
          terminal=!body->continuation;
          if(const auto* values=std::get_if<azureus::lookup::FoundValues>(&body->result)){
            static_cast<void>(lookup_->values(node,values->values,body->continuation,values->diversification,now));
          }else if(const auto* contacts=std::get_if<azureus::lookup::FoundContacts>(&body->result)){
            if(contacts->providerFailures){
              if(body->continuation){static_cast<void>(lookup_->contacts(node,{},true,now));}
              else{static_cast<void>(lookup_->finish(node,control::LookupNodeState::Failed,now));}
            }
            else{
              std::vector<control::AdmittedContact> admitted;admitted.reserve(contacts->contacts.size());
              for(const auto& contact:contacts->contacts){admitted.push_back(lookupAdmission(contact));}
              static_cast<void>(lookup_->contacts(node,admitted,body->continuation,now));
            }
          }
        }else{static_cast<void>(lookup_->finish(node,control::LookupNodeState::Failed,now));}
        if(terminal){*found={};}
        pumpLookup();publish();
      }catch(...){failLookup(AzureusServiceError::Memory);}
    }
    void pumpLookup()noexcept{
      if(!lookup_||!lookupCommand_||lookupResultPublished_){return;}
      try{
        const auto now=lookupNow();auto intents=lookup_->takeIntents(now);
        if(!intents){failLookup(AzureusServiceError::Transport);return;}
        for(const auto& intent:*intents){
          auto free=std::ranges::find(lookupRpcs_,std::uint64_t{0},&LookupRpc::node);
          const auto peer=lookupPeer(intent.contact.address);
          if(free==lookupRpcs_.end()||!peer||!validPeer(*peer,options_.bindAddress)||!isOwned(*peer)){
            static_cast<void>(lookup_->finish(intent.node,control::LookupNodeState::Rejected,lookupNow()));continue;
          }
          const auto remaining=lookupTimeoutMillis_-(lookupNow()-lookupStartedMillis_);
          if(remaining<=0){static_cast<void>(lookup_->finish(intent.node,control::LookupNodeState::Rejected,lookupNow()));continue;}
          if(options_.maintenance&&router_&&!router_->recordLookup(intent.target,QDateTime::currentMSecsSinceEpoch(),routerNow())){failLookup(AzureusServiceError::Transport);return;}
          free->node=intent.node;free->peer=*peer;const auto operation=lookupCommand_->id;
          auto ticket=actor_.findValue(*peer,intent.contact.protocol,intent.target,intent.flags,intent.maximumValues,this,
              [this,operation,node=intent.node](azureus::FindValueOutcome result){lookupReply(operation,node,std::move(result));},std::chrono::milliseconds(remaining));
          if(!ticket){*free={};static_cast<void>(lookup_->finish(intent.node,control::LookupNodeState::Failed,lookupNow()));continue;}
          free->ticket=*ticket;
          if(!lookup_->sent(intent.node,ticket->value,lookupNow())){failLookup(AzureusServiceError::Transport);return;}
        }
        auto cancellations=lookup_->cancellationIntents();if(!cancellations){failLookup(AzureusServiceError::Memory);return;}
        for(const auto& intent:*cancellations){
          auto rpc=std::ranges::find(lookupRpcs_,intent.node,&LookupRpc::node);
          if(rpc!=lookupRpcs_.end()&&rpc->ticket.value==intent.ticket&&!rpc->cancellationRequested){
            rpc->cancellationRequested=true;static_cast<void>(actor_.cancel(rpc->ticket));
            // false can mean a real reply is already queued. Do not fabricate
            // cancellation acknowledgement; its actual callback settles it.
          }
        }
        finishLookup();
      }catch(...){failLookup(AzureusServiceError::Memory);}
    }
    void beginLookup(const Command& command)noexcept{
      if(lookupCommand_&&!lookupResultPublished_){failCommand(command,AzureusServiceError::Capacity);return;}
      if(!isOwned(command.peer)){complete(command,std::unexpected(azureus::Failure::Peer));return;}
      try{
        const auto transport=actor_.statisticsSnapshot();const auto local=routerIdentity(actor_.endpoint(),options_.version);
        const auto seedId=routerIdentity(command.peer,options_.version);
        if(!transport||!local||!seedId){failCommand(command,AzureusServiceError::Transport);return;}
        nodes::Contact seed;seed.id=*seedId;seed.protocol=options_.version;seed.address.port=command.peer.port;
        if(command.peer.address.protocol()==QAbstractSocket::IPv4Protocol){seed.address.length=4;const auto address=command.peer.address.toIPv4Address();for(unsigned i=0;i<4;++i){seed.address.bytes[i]=static_cast<std::uint8_t>(address>>(24-8*i));}}
        else{seed.address.length=16;const auto address=command.peer.address.toIPv6Address();std::copy_n(address.c,16,seed.address.bytes.begin());}
        control::LookupOptions options;options.target=command.node;options.localId=*local;options.flags=static_cast<std::uint8_t>(command.flags);
        options.maximumValues=static_cast<std::size_t>(command.maximumValues);options.timeoutMillis=std::chrono::ceil<std::chrono::milliseconds>(command.timeout).count();
        auto candidate=std::make_unique<control::LookupController>();const std::array seeds{control::AdmittedContact{seed,true}};
        const auto now=lookupNow();auto started=candidate->start(transport->generation(),command.id,options,seeds,now);
        if(!started){failCommand(command,AzureusServiceError::Invalid);return;}
        if(options_.maintenance&&router_&&!router_->lookupStarted(QDateTime::currentMSecsSinceEpoch(),routerNow())){failCommand(command,AzureusServiceError::Transport);return;}
        lookup_=std::move(candidate);lookupCommand_=command;lookupResultPublished_=false;lookupSnapshot_.reset();lookupFailure_.reset();
        lookupStartedMillis_=now;lookupTimeoutMillis_=options.timeoutMillis;lookupRpcs_={};pumpLookup();publish();
      }catch(...){lookup_.reset();lookupSnapshot_.reset();lookupFailure_=AzureusServiceError::Memory;failCommand(command,AzureusServiceError::Memory);}
    }

    static azureus::Config configuration(const AzureusServiceOptions& options){azureus::Config config;config.network=options.network;config.version=options.version;
      config.routingScope=options.routingScope;config.servePings=options.servePings;config.observeOperations=true;config.enableVivaldi=options.enableVivaldi;config.maximumPending=options.maximumPending;config.maximumPeers=options.maximumPeers;return config;}
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
        captureOwnedRouting();captureMaintenance();captureRouter();captureLookup();refreshRatingObservation();
        if(localDatabase_){const auto value=localDatabase_->snapshot();if(value){auto copy=std::make_shared<const services::azureus_db_runtime::Snapshot>(*value);std::lock_guard lock(state_->mutex);state_->value.databaseSnapshot=std::move(copy);}}
        if(last_&&sameFacts(*last_,*actual)){std::lock_guard lock(state_->mutex);const bool current=!state_->stop&&state_->value.state==AzureusServiceState::Running&&actual->transport().state()==azureus::TransportStatisticsState::Bound;
          if(state_->value.current!=current||state_->value.error||state_->value.routerSnapshot!=routerSnapshot_||state_->value.routerFailure!=routerFailure_||state_->value.lookupSnapshot!=lookupSnapshot_||state_->value.lookupFailure!=lookupFailure_){
            state_->value.current=current;state_->value.error.reset();state_->value.transportFailure.reset();
            state_->value.routerSnapshot=routerSnapshot_;state_->value.routerFailure=routerFailure_;state_->value.lookupSnapshot=lookupSnapshot_;state_->value.lookupFailure=lookupFailure_;state_->advance();}return;}
        auto copied=std::make_shared<const azureus::OwnedDhtStatisticsSnapshot>(*actual);
        std::lock_guard lock(state_->mutex);last_=copied;state_->value.snapshot=std::move(copied);state_->value.routerSnapshot=routerSnapshot_;state_->value.routerFailure=routerFailure_;state_->value.lookupSnapshot=lookupSnapshot_;state_->value.lookupFailure=lookupFailure_;state_->value.current=!state_->stop&&state_->value.state==AzureusServiceState::Running&&actual->transport().state()==azureus::TransportStatisticsState::Bound;state_->value.error.reset();state_->value.transportFailure.reset();state_->advance();
      }catch(...){std::lock_guard lock(state_->mutex);state_->value.current=false;state_->value.error=AzureusServiceError::Memory;state_->advance();}
    }
    std::shared_ptr<State> state_;
    AzureusServiceOptions options_;
    QEventLoop& loop_;
    azureus::OwnedPingTransport actor_;
    QChronoTimer maintenanceTimer_;
    std::array<Active,32> active_{};
    std::array<std::optional<azureus::Peer>,32> ownedPeers_{};
    std::array<LookupRpc,control::LookupController::MaximumParallelism> lookupRpcs_{};
    std::unique_ptr<control::LookupController> lookup_;
    std::optional<Command> lookupCommand_;
    bool lookupResultPublished_=true;
    std::int64_t lookupStartedMillis_=0,lookupTimeoutMillis_=0;
    std::shared_ptr<const control::LookupSnapshot> lookupSnapshot_;
    std::optional<AzureusServiceError> lookupFailure_;
    std::shared_ptr<const azureus::OwnedDhtStatisticsSnapshot> last_;
    nodes::NodeCache nodeCache_; // Constructed, used and destroyed on this owner.
    Clock::time_point routerStarted_{};
    std::shared_ptr<router::RouterOwner> router_;
    std::shared_ptr<const router::RouterStatisticsSnapshot> routerSnapshot_;
    std::optional<router::RouterError> routerFailure_;
  };
  class Thread final:public QThread {
  public:
    Thread(std::shared_ptr<State> state,AzureusServiceOptions options):state_(std::move(state)),options_(std::move(options)){}
  protected:
    void run()override{
      try{
        QEventLoop loop;Owner owner(state_,options_,loop);struct Unbind{std::shared_ptr<State> state;~Unbind(){if(state->profileIo){state->profileIo->unbind();}std::lock_guard lock(state->mutex);state->owner=nullptr;state->dispatcher=nullptr;}} unbind{state_};auto* dispatcher=QAbstractEventDispatcher::instance();const auto started=owner.start();
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
  explicit Impl(AzureusServiceOptions options):options(std::move(options)),thread(state,this->options){
    if(this->options.valueVersions){state->profileIo=std::make_shared<detail::AzureusProfileIo>(this->options.valueVersions->profileOwnership());}
  }
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
    auto service=std::unique_ptr<OwnedAzureusService>(new OwnedAzureusService(std::make_unique<Impl>(std::move(options))));
    if(service->impl_->state->profileIo&&!service->impl_->state->profileIo->initialize(service->impl_->options.valueVersions,service->impl_->options.contacts)){return std::unexpected(AzureusServiceError::Profile);}
    service->impl_->thread.start();
    auto& state=*service->impl_->state;std::unique_lock lock(state.mutex);
    const bool ready=state.readyCondition.wait_for(lock,std::chrono::seconds(5),[&]{return state.ready;});
    const auto error=!ready?std::optional{AzureusServiceError::Start}:state.value.state==AzureusServiceState::Failed?state.value.error:std::nullopt;lock.unlock();
    if(error){(void)service->close();return std::unexpected(*error);}return service;
  }catch(...){return std::unexpected(AzureusServiceError::Memory);}
}
OwnedAzureusService::~OwnedAzureusService(){(void)close();}
azureus::Peer OwnedAzureusService::endpoint()const{std::lock_guard lock(impl_->state->mutex);return impl_->state->endpoint;}
AzureusServiceObservation OwnedAzureusService::observation()const{
  std::lock_guard lock(impl_->state->mutex);auto value=impl_->state->value;
  if(impl_->state->profileIo){const auto facts=impl_->state->profileIo->facts();value.profileIoPending=facts.pending;value.profileIoRunning=facts.running;value.profileIoReady=facts.ready;value.profileIoHighWater=facts.highWater;}
  if(value.ratingReadSnapshot&&value.ratingReadSnapshot->current&&impl_->state->profileIo
      &&(impl_->state->stop||impl_->state->ratingRevision!=impl_->state->profileIo->lease()->sourceRevision())){
    auto withdrawn=std::make_shared<AzureusRatingReadSnapshot>(*value.ratingReadSnapshot);withdrawn->current=false;withdrawn->ratings=plugins::rating::RatingResults{};withdrawn->localFallback=false;withdrawn->state=AzureusRatingReadState::Failed;withdrawn->error=AzureusServiceError::Profile;value.ratingReadSnapshot=std::move(withdrawn);
  }return value;
}
std::expected<std::uint64_t,AzureusServiceError> OwnedAzureusService::submit(Command value){
  auto& state=*impl_->state;std::lock_guard lock(state.mutex);
  if(state.stop||state.value.state!=AzureusServiceState::Running||!state.owner){return std::unexpected(AzureusServiceError::Closed);}
  const bool contactCommand=value.kind==AzureusServiceCommand::BootstrapSaved||value.kind==AzureusServiceCommand::FlushContacts;
  if(value.kind!=AzureusServiceCommand::Cancel&&!contactCommand&&!validPeer(value.peer,state.endpoint.address)){return std::unexpected(AzureusServiceError::Invalid);}
  if(value.kind==AzureusServiceCommand::Cancel&&!value.target){return std::unexpected(AzureusServiceError::Invalid);}
  if(value.kind!=AzureusServiceCommand::AllowPeer&&value.kind!=AzureusServiceCommand::Cancel&&(value.timeout<=std::chrono::nanoseconds::zero()||value.timeout>std::chrono::seconds(30))){return std::unexpected(AzureusServiceError::Invalid);}
  if((value.kind==AzureusServiceCommand::PublishRating||value.kind==AzureusServiceCommand::ReadRatings)
      &&(!impl_->options.valueVersions||impl_->options.network!=0||!value.ratingHash||value.timeout<std::chrono::milliseconds(1)
          ||(value.kind==AzureusServiceCommand::ReadRatings&&value.timeout>std::chrono::seconds(20)))){return std::unexpected(AzureusServiceError::Invalid);}
  if((value.kind==AzureusServiceCommand::IntegrateRouting||value.kind==AzureusServiceCommand::ForwardCached)
      &&(impl_->options.routingScope!=azureus::RoutingScope::OwnedLoopback||!impl_->options.serveValues||!impl_->options.valueVersions||value.timeout<std::chrono::milliseconds(1))){return std::unexpected(AzureusServiceError::Invalid);}
  if(value.kind==AzureusServiceCommand::ForwardCached&&(value.applicationKey.empty()||value.applicationKey.size()>1024)){return std::unexpected(AzureusServiceError::Invalid);}
  if(contactCommand&&(!impl_->options.maintenance||!impl_->options.contacts||!impl_->options.valueVersions)){return std::unexpected(AzureusServiceError::Invalid);}
  if(value.publicationFlags!=AzureusPublicationFlags::Normal&&value.publicationFlags!=AzureusPublicationFlags::Anonymous){return std::unexpected(AzureusServiceError::Invalid);}
  if(value.kind==AzureusServiceCommand::PublishValue&&(!impl_->options.valueVersions||value.applicationKey.empty()||value.applicationKey.size()>1024||value.payload.empty()||value.payload.size()>512||value.timeout<std::chrono::milliseconds(1))){return std::unexpected(AzureusServiceError::Invalid);}
  if(value.kind==AzureusServiceCommand::FindValue&&(value.maximumValues<0||value.maximumValues>255)){return std::unexpected(AzureusServiceError::Invalid);}
  if(value.kind==AzureusServiceCommand::LookupValue&&((value.flags!=0&&value.flags!=8)||value.maximumValues<1||value.maximumValues>64||value.timeout<std::chrono::milliseconds(1))){return std::unexpected(AzureusServiceError::Invalid);}
  if(state.value.outstandingCommands>=impl_->options.maximumCommands||state.nextRequest==std::numeric_limits<std::uint64_t>::max()){return std::unexpected(AzureusServiceError::Capacity);}
  value.id=++state.nextRequest;++state.value.outstandingCommands;auto* owner=static_cast<Impl::Owner*>(state.owner);
  try{if(!QMetaObject::invokeMethod(owner,[owner,value]{owner->command(value);},Qt::QueuedConnection)){--state.value.outstandingCommands;return std::unexpected(AzureusServiceError::Dispatch);}}
  catch(...){--state.value.outstandingCommands;return std::unexpected(AzureusServiceError::Memory);}state.advance();return value.id;
}
std::expected<std::uint64_t,AzureusServiceError> OwnedAzureusService::allowPeer(azureus::Peer peer){Command value;value.kind=AzureusServiceCommand::AllowPeer;value.peer=std::move(peer);return submit(value);}
std::expected<std::uint64_t,AzureusServiceError> OwnedAzureusService::ping(azureus::Peer peer,std::chrono::nanoseconds timeout){Command value;value.peer=std::move(peer);value.timeout=timeout;return submit(value);}
std::expected<std::uint64_t,AzureusServiceError> OwnedAzureusService::findNode(azureus::Peer peer,azureus::find::nodes::NodeId target,std::chrono::nanoseconds timeout){Command value;value.kind=AzureusServiceCommand::FindNode;value.peer=std::move(peer);value.node=target;value.timeout=timeout;return submit(value);}
std::expected<std::uint64_t,AzureusServiceError> OwnedAzureusService::findValue(azureus::Peer peer,azureus::lookup::nodes::NodeId target,std::int32_t flags,std::int32_t maximumValues,std::chrono::nanoseconds timeout){Command value;value.kind=AzureusServiceCommand::FindValue;value.peer=std::move(peer);value.node=target;value.flags=flags;value.maximumValues=maximumValues;value.timeout=timeout;return submit(value);}
std::expected<std::uint64_t,AzureusServiceError> OwnedAzureusService::lookupValue(azureus::Peer seed,azureus::lookup::nodes::NodeId target,std::int32_t flags,std::int32_t maximumValues,std::chrono::nanoseconds timeout){Command value;value.kind=AzureusServiceCommand::LookupValue;value.peer=std::move(seed);value.node=target;value.flags=flags;value.maximumValues=maximumValues;value.timeout=timeout;return submit(value);}
std::expected<std::uint64_t,AzureusServiceError> OwnedAzureusService::publishValue(azureus::Peer peer,std::string key,std::string payload,std::chrono::nanoseconds timeout){return publishValue(std::move(peer),std::move(key),std::move(payload),timeout,AzureusPublicationFlags::Normal);}
std::expected<std::uint64_t,AzureusServiceError> OwnedAzureusService::publishValue(azureus::Peer peer,std::string key,std::string payload,std::chrono::nanoseconds timeout,AzureusPublicationFlags flags){Command value;value.kind=AzureusServiceCommand::PublishValue;value.peer=std::move(peer);value.applicationKey=std::move(key);value.payload=std::move(payload);value.timeout=timeout;value.publicationFlags=flags;return submit(std::move(value));}
std::expected<std::uint64_t,AzureusServiceError> OwnedAzureusService::publishSavedRating(azureus::Peer peer,core::TorrentHash hash,std::array<std::uint8_t,32> revision,std::chrono::nanoseconds timeout){Command value;value.kind=AzureusServiceCommand::PublishRating;value.peer=std::move(peer);value.ratingHash=std::move(hash);value.ratingRevision=revision;value.timeout=timeout;return submit(std::move(value));}
std::expected<std::uint64_t,AzureusServiceError> OwnedAzureusService::readSavedRatings(azureus::Peer peer,core::TorrentHash hash,std::array<std::uint8_t,32> revision,std::chrono::nanoseconds timeout){Command value;value.kind=AzureusServiceCommand::ReadRatings;value.peer=std::move(peer);value.ratingHash=std::move(hash);value.ratingRevision=revision;value.timeout=timeout;return submit(std::move(value));}
std::expected<std::uint64_t,AzureusServiceError> OwnedAzureusService::integrateRouting(azureus::Peer seed,std::chrono::nanoseconds timeout){Command value;value.kind=AzureusServiceCommand::IntegrateRouting;value.peer=std::move(seed);value.timeout=timeout;return submit(std::move(value));}
std::expected<std::uint64_t,AzureusServiceError> OwnedAzureusService::forwardCachedValue(azureus::Peer peer,std::string key,std::chrono::nanoseconds timeout){Command value;value.kind=AzureusServiceCommand::ForwardCached;value.peer=std::move(peer);value.applicationKey=std::move(key);value.timeout=timeout;return submit(std::move(value));}
std::expected<std::uint64_t,AzureusServiceError> OwnedAzureusService::bootstrapSavedContacts(std::chrono::nanoseconds timeout){Command value;value.kind=AzureusServiceCommand::BootstrapSaved;value.timeout=timeout;return submit(std::move(value));}
std::expected<std::uint64_t,AzureusServiceError> OwnedAzureusService::flushContacts(std::chrono::nanoseconds timeout){Command value;value.kind=AzureusServiceCommand::FlushContacts;value.timeout=timeout;return submit(std::move(value));}
std::expected<std::uint64_t,AzureusServiceError> OwnedAzureusService::cancel(std::uint64_t request){Command value;value.kind=AzureusServiceCommand::Cancel;value.target=request;return submit(value);}
std::expected<void,AzureusServiceError> OwnedAzureusService::close(){
  if(QThread::currentThread()==&impl_->thread){return std::unexpected(AzureusServiceError::Thread);}std::lock_guard lifecycle(impl_->closeMutex);auto& state=*impl_->state;
  {std::lock_guard lock(state.mutex);if(state.value.joined){return {}; }state.stop=true;state.value.current=false;if(state.value.state==AzureusServiceState::Running){state.value.state=AzureusServiceState::Closing;state.advance();}if(state.dispatcher){state.dispatcher->wakeUp();}}
  if(state.profileIo){state.profileIo->requestStop();}
  if(!impl_->thread.wait()){return std::unexpected(AzureusServiceError::Thread);}
  if(state.profileIo){state.profileIo->join();}
  std::lock_guard lock(state.mutex);state.value.joined=true;state.advance();return {};
}
} // namespace biglybtc::app
