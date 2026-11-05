// Test-only real source-permit driver, never a Current/grant setter.
#pragma once
#include "actor.hpp"
#include "source_permit.hpp"
#include <QDateTime>
#include <QPointer>
#include <optional>
#include <stdexcept>
namespace biglybtc::p07_test {
namespace i2p=services::i2p;namespace cp=i2p::i2cp;namespace provider=i2p::provider;
class InboundSourceDriver final:public QObject {
public:
  using Phase=provider::SourcePermit::Phase;
  struct Observation {Phase phase=Phase::Idle;std::uint64_t heldTurns=0,createRequests=0;bool paused=false,failed=false,validationFailed=false;};
  InboundSourceDriver(std::shared_ptr<provider::ProfileSource> source,cp::InitialChannel& channel)
      :channel_(channel),permit_(source,this){
    if(!source||!channel_.bindEffectAuthority(source->effectAuthority())){throw std::runtime_error("Real isolated source authority required");}
    QObject::connect(&channel_,&cp::InitialChannel::sourceEffectRequested,this,[this]{request();});
  }
  ~InboundSourceDriver()override{actor_.clear();permit_.stop();permit_.join();}
  void setActor(i2p::dht::Actor* actor){actor_=actor;}
  bool queueCreate(i2p::ClientIdentity&& identity,cp::SessionOptions options){
    if(identity_||createRequests_||!identity.valid()||channel_.state()!=cp::InitialChannel::State::DateReceived){return false;}
    identity_.emplace(std::move(identity));options_=std::move(options);request();return true;
  }
  void request(){if(!paused_&&!failed_&&permit_.phase()==Phase::Idle){permit_.request();}}
  // Pause effects before asynchronous fixture mutation. A Ready permit is
  // consumed/ACKed without channel/Actor calls, so its real worker releases the
  // gate and cannot deadlock the mutator. No new request is made until resume.
  void pause(bool value){paused_=value;drain();if(!paused_){request();}}
  Observation observation()const{return {permit_.phase(),heldTurns_,createRequests_,paused_,failed_,validationFailed_};}
  std::optional<i2p::dht::Actor::InboundObservation> lastHeldInbound()const{return lastHeld_;}
  void withdraw(){if(actor_){actor_->close();}channel_.close();permit_.stop();}
  void join(){permit_.join();} // External lifetime, only after withdraw.
protected:
  bool event(QEvent* event)override{
    if(event->type()!=provider::SourcePermit::wakeEvent()){return QObject::event(event);}
    try{drain();if(!paused_&&(identity_||channel_.sourceEffectsPending())){request();}}
    catch(...){failed_=true;withdraw();}return true;
  }
private:
  cp::InitialChannel& channel_;provider::SourcePermit permit_;
  QPointer<i2p::dht::Actor> actor_;std::optional<i2p::ClientIdentity> identity_;
  cp::SessionOptions options_;std::optional<i2p::dht::Actor::InboundObservation> lastHeld_;
  std::uint64_t heldTurns_=0,createRequests_=0;bool paused_=false,failed_=false,validationFailed_=false;
  void drain(){
    if(permit_.phase()==Phase::Failed){failed_=validationFailed_=true;withdraw();return;}
    if(failed_||permit_.phase()!=Phase::Ready){return;}const auto revision=permit_.take();if(!revision){
      // Permanent revoke can settle Failed inside take(); its stopped worker
      // need not publish another wake. Withdraw in this same refusal turn.
      const auto phase=permit_.phase();if(phase==Phase::Failed||phase==Phase::Stopped){failed_=validationFailed_=true;withdraw();}return;
    }
    struct Ack{provider::SourcePermit& permit;~Ack(){permit.acknowledge();}} ack{permit_};
    ++heldTurns_;if(paused_){return;}
    if(!channel_.resumeAuthorizedEffects()){failed_=true;channel_.close();return;}
    if(identity_){
      const auto wall=QDateTime::currentMSecsSinceEpoch();if(wall<=0){failed_=true;channel_.close();return;}
      auto created=channel_.createLeaseSession(std::move(*identity_),options_,static_cast<std::uint64_t>(wall));identity_.reset();++createRequests_;
      if(!created){failed_=true;channel_.close();return;}
    }
    if(actor_){actor_->resumeAuthorizedInbound();lastHeld_=actor_->inboundObservation();}
  }
};
}
