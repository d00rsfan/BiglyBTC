// SPDX-License-Identifier: GPL-2.0-or-later
#include "actor.hpp"
#include <QMetaObject>
#include <QPointer>
#include <QThread>
#include <QTimer>
#include <openssl/rand.h>
#include <algorithm>
#include <chrono>
#include <limits>
#include <new>
namespace biglybtc::services::i2p::dht {
struct Actor::Impl {
  using Clock=std::chrono::steady_clock;
  Actor* q;QPointer<i2cp::InitialChannel> channel;QTimer timer;Options options;
  Lookup lookup;std::uint64_t nextOperation=1,scope=0,resolver=0;std::optional<std::size_t> resolving;
  std::vector<std::array<Byte,8>> transactions;Bytes localDestination;Hash localHash{};
  bool enabled=false,pumping=false,wakeQueued=false,notificationQueued=false;
  explicit Impl(Actor* owner,i2cp::InitialChannel& c):q(owner),channel(&c),timer(owner){
    timer.setSingleShot(true);timer.setTimerType(Qt::PreciseTimer);transactions.reserve(1024);
    QObject::connect(&timer,&QTimer::timeout,q,[this]{safePump();});
    QObject::connect(&c,&i2cp::InitialChannel::dhtChanged,q,[this]{schedule();});
    QObject::connect(&c,&i2cp::InitialChannel::destinationLookupChanged,q,[this]{schedule();});
    QObject::connect(&c,&i2cp::InitialChannel::leaseSetSubmitted,q,[this]{schedule();});
    QObject::connect(&c,&i2cp::InitialChannel::stateChanged,q,[this]{schedule();});
    QObject::connect(&c,&i2cp::InitialChannel::closed,q,[this]{safeClose();});
    QObject::connect(&c,&i2cp::InitialChannel::failed,q,[this]{safeClose();});
    QObject::connect(&c,&QObject::destroyed,q,[this]{channel=nullptr;safeClose();});
  }
  bool onThread()const{return QThread::currentThread()==q->thread()&&(!channel||channel->thread()==q->thread());}
  static std::int64_t now(){return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count();}
  void notify(){
    if(notificationQueued){return;}notificationQueued=true;
    const QPointer<Actor> owner(q);if(!QMetaObject::invokeMethod(q,[owner]{if(!owner){return;}owner->impl_->notificationQueued=false;emit owner->changed();},Qt::QueuedConnection)){notificationQueued=false;throw std::bad_alloc();}
  }
  void schedule(){
    if(wakeQueued){return;}wakeQueued=true;const QPointer<Actor> owner(q);
    if(!QMetaObject::invokeMethod(q,[owner]{if(!owner){return;}owner->impl_->wakeQueued=false;owner->impl_->safePump();},Qt::QueuedConnection)){wakeQueued=false;safeClose();}
  }
  bool current(){
    if(!enabled||!channel||scope!=channel->dhtScopeGeneration()){return false;}
    if(channel->state()==i2cp::InitialChannel::State::Reconfiguring){return true;}
    auto identity=channel->ownedLeaseDestination();return identity&&identity->wire==localDestination;
  }
  void cancelPending(){
    if(channel){if(resolver){channel->cancelDestinationLookup(resolver);}for(const auto& n:lookup.nodes()){if(n.ticket){channel->cancelDhtDatagram(n.ticket);}}}
    resolver=0;resolving.reset();
  }
  void close(){
    const auto ownedScope=scope;
    timer.stop();cancelPending();lookup.close();enabled=false;scope=0;localDestination.clear();
    if(channel&&onThread()&&ownedScope){channel->disableDhtDatagrams(ownedScope);}notify();
  }
  void safeClose()noexcept{try{close();}catch(...){timer.stop();enabled=false;scope=0;lookup.close();}}
  void safePump()noexcept{try{pump();}catch(...){safeClose();}}
  Result<Bytes> transaction(){
    if(transactions.size()==1024){return std::unexpected(Error{ErrorCode::TooLarge,"DHT lifetime transaction capacity exhausted"});}
    std::array<Byte,8> tx{};for(unsigned tries=0;tries<8;++tries){if(RAND_bytes(tx.data(),int(tx.size()))!=1){break;}
      if(std::find(transactions.begin(),transactions.end(),tx)==transactions.end()){transactions.push_back(tx);return Bytes(tx.begin(),tx.end());}}
    return std::unexpected(Error{ErrorCode::CryptoFailure,"DHT transaction entropy unavailable"});
  }
  bool begin(const Options& value){
    if(!onThread()||enabled||!channel||value.seeds.empty()||value.seeds.size()>20||!value.parallelism||value.parallelism>3){return false;}
    auto identity=channel->ownedLeaseDestination();if(!identity){return false;}
    Contact local{value.localId,identity->hash,value.queryPort,{}};if(!validContact(local)){return false;}
    if(std::any_of(value.seeds.begin(),value.seeds.end(),[&](const auto& c){return !validContact(c)||c.hash==identity->hash;})){return false;}
    options=value;
    for(auto& c:options.seeds){if(c.destination){auto parsed=decodeDestination(c.destination->wire);if(!parsed||parsed->hash!=c.hash){return false;}c.destination=std::move(*parsed);}}
    localDestination=identity->wire;localHash=identity->hash;
    if(!channel->enableDhtDatagrams(value.queryPort)){return false;}scope=channel->dhtScopeGeneration();enabled=true;notify();return true;
  }
  Result<std::uint64_t> start(const Id& target,bool seed,bool announce,unsigned timeout){
    if(!onThread()||!current()||lookup.running()||nextOperation>1024){return std::unexpected(Error{ErrorCode::InvalidFormat,"Current idle explicitly bound DHT actor required"});}
    cancelPending();while(channel->takeDhtDatagram()){}while(channel->takeDhtDispatch()){}
    const auto operation=nextOperation;
    if(!lookup.start(operation,{target,seed,announce,timeout,options.parallelism},options.seeds,now())){return std::unexpected(Error{ErrorCode::InvalidFormat,"Invalid finite DHT lookup options"});}
    ++nextOperation;pump();return operation;
  }
  void pump(){
    if(pumping||!onThread()||!enabled){return;}
    if(!current()){close();return;}pumping=true;struct Guard{bool& v;~Guard(){v=false;}} guard{pumping};
    lookup.tick(now());
    for(std::size_t steps=0;steps<8;++steps){auto result=channel->takeDhtDispatch();if(!result){break;}lookup.dispatched(result->ticket,result->written,now());}
    for(std::size_t steps=0;steps<8;++steps){auto data=channel->takeDhtDatagram();if(!data){break;}if(data->sender){continue;}auto message=decode(data->payload);if(message){std::erase_if(message->nodes,[&](const auto& c){return c.hash==localHash||c.id==options.localId;});lookup.reply(*message,data->fromPort,now());}}
    if(resolver){
      auto result=channel->destinationLookupObservation(resolver);
      if(result&&result->operation==resolver&&result->phase!=i2cp::InitialChannel::LookupPhase::Pending){
        if(resolving&&result->phase==i2cp::InitialChannel::LookupPhase::Resolved&&result->destination){if(!lookup.resolved(*resolving,*result->destination)){lookup.failed(*resolving);}}
        else if(resolving){
          lookup.failed(*resolving);
        }
        resolver=0;resolving.reset();
      }
    }
    // At most three new RPCs per turn. No reentrant callback/foreign code is
    // invoked; channel signals are queued current-state notifications.
    for(unsigned steps=0;steps<3&&lookup.running()&&channel->state()==i2cp::InitialChannel::State::SessionCreated;++steps){
      auto index=lookup.next(now());if(!index){break;}const auto node=lookup.nodes().at(*index);
      if(!node.contact.destination){
        if(resolver){break;}
        // The provider shares this channel's single HostLookup slot. Yield to
        // its real completion notification, keeping this lookup's original end.
        const auto currentLookup=channel->destinationLookupObservation();
        if(currentLookup&&currentLookup->phase==i2cp::InitialChannel::LookupPhase::Pending){break;}
        auto op=channel->lookupDestination(node.contact.hash,30000);
        if(!op){lookup.failed(*index);continue;}if(!lookup.resolving(*index)){channel->cancelDestinationLookup(*op);continue;}resolver=*op;resolving=*index;continue;
      }
      const auto intended=now();auto tx=transaction();if(!tx){close();return;}
      Message message;message.transaction=*tx;message.id=options.localId;message.target=lookup.options().target;
      message.method=node.phase==NodePhase::Replied?Method::AnnouncePeer:Method::GetPeers;message.noSeed=lookup.options().seed;message.seed=lookup.options().seed;message.token=node.token;
      auto packet=encode(message);if(!packet){lookup.failed(*index);continue;}
      const auto left=lookup.end()-now();if(left<=0){lookup.tick(now());break;}
      // announce_peer is raw18 to target Q+1; get_peers is signed17 to Q.
      const bool signedQuery=message.method!=Method::AnnouncePeer;
      const auto port=std::uint16_t(node.contact.port+(signedQuery?0:1));
      auto ticket=channel->submitDhtDatagram(node.contact.destination->wire,*packet,signedQuery,port,unsigned(std::min<std::int64_t>(30000,left)));
      if(!ticket){lookup.failed(*index);continue;}if(!lookup.queued(*index,std::move(*tx),*ticket,intended)){channel->cancelDhtDatagram(*ticket);}
    }
    lookup.tick(now());
    if(!lookup.running()){timer.stop();cancelPending();notify();return;}
    auto end=lookup.end();for(const auto& n:lookup.nodes()){if(n.phase==NodePhase::Resolving||n.phase==NodePhase::Queued||n.phase==NodePhase::Sent){end=std::min(end,n.end);}}
    timer.start(int(std::max<std::int64_t>(1,end-now())));notify();
  }
};
Actor::Actor(i2cp::InitialChannel& c,QObject* parent):QObject(parent),impl_(std::make_unique<Impl>(this,c)){}
Actor::~Actor(){impl_->safeClose();}
bool Actor::begin(const Options& value){try{return impl_->begin(value);}catch(...){impl_->safeClose();return false;}}
Result<std::uint64_t> Actor::lookupPeers(const Id& target,bool seed,unsigned timeout){try{return impl_->start(target,seed,false,timeout);}catch(...){impl_->safeClose();return std::unexpected(Error{ErrorCode::CryptoFailure,"DHT actor resources unavailable"});}}
Result<std::uint64_t> Actor::announceSelf(const Id& target,bool seed,unsigned timeout){try{return impl_->start(target,seed,true,timeout);}catch(...){impl_->safeClose();return std::unexpected(Error{ErrorCode::CryptoFailure,"DHT actor resources unavailable"});}}
bool Actor::cancel(std::uint64_t op){try{if(!impl_->onThread()||!impl_->lookup.running()||impl_->lookup.snapshot().operation!=op){return false;}impl_->lookup.cancel();impl_->cancelPending();impl_->timer.stop();impl_->notify();return true;}catch(...){impl_->safeClose();return false;}}
void Actor::close(){if(impl_->onThread()){impl_->safeClose();}}
Snapshot Actor::observation()const{if(!impl_->onThread()){return {};}return impl_->lookup.snapshot();}
} // namespace biglybtc::services::i2p::dht
