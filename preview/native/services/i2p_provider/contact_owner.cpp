// SPDX-License-Identifier: GPL-2.0-or-later
#include "contact_owner.hpp"
#include <limits>

namespace biglybtc::services::i2p::provider {
namespace {
using Error=storage::Error;using Code=storage::ErrorCode;
constexpr auto Check=std::chrono::seconds(15);
}
ContactExchange::ContactExchange(std::shared_ptr<const storage::ProfileLease> lease,unsigned index):lease_(std::move(lease)),index_(index){}
void ContactExchange::tick(Clock::time_point now)noexcept{
  if(stopped_.load()||now<next_){return;}
  if(now>Clock::time_point::max()-Check){stop();return;}next_=now+Check;
  // Original DHTI2P checks every15s and exports every8 delivered ticks. No
  // elapsed-time catchup loop or cache write is executed on this thread.
  if(++ticks_<8){return;}ticks_=0;const auto old=wanted_.load();
  if(old==std::numeric_limits<std::uint64_t>::max()){stop();return;}wanted_.store(old+1);
}
std::optional<ContactExchange::Clock::time_point> ContactExchange::nextCheck()const noexcept{return stopped_.load()?std::nullopt:std::optional{next_};}
bool ContactExchange::pending()const noexcept{return !stopped_.load()&&wanted_.load()!=completed_.load();}
bool ContactExchange::capture(dht::Actor& actor,const i2cp::InitialChannel& channel,std::shared_ptr<ProfileSource> source){
  try{
  if(!pending()||!source||!source->lease()||!source->lease()->sharesOwnership(*lease_)||source->mixedMode()!=(index_==0)){return false;}
  if(!actor.boundChannel(channel)){return false;}const auto identity=channel.ownedLeaseDestination();if(!identity||identity->wire!=source->destination().wire){return false;}
  const auto state=actor.inboundObservation();const auto scope=channel.dhtScopeGeneration();const auto revision=lease_->sourceRevision();
  const auto current=[&]{return !stopped_.load()&&scope&&state.scope==scope&&channel.dhtScopeGeneration()==scope
      &&state.ready&&state.sourceCurrent&&state.grantActive&&source->revisionCurrent(revision)
      &&channel.dhtScopeObservation(scope).granted;};
  if(!current()){return false;}auto rows=actor.exportSavedContacts();if(!rows||rows->size()>dht::MaximumSavedContacts){return false;}
  const auto wanted=wanted_.load();auto value=std::make_shared<const Export>(Export{std::move(*rows),source,wanted,revision,scope,source->destination().hash,wanted>1});
  if(!current()){return false;}latest_.store(std::move(value));completed_.store(wanted);return true;
  }catch(...){failed_.store(true);stop();return false;}
}
void ContactExchange::stop()noexcept{stopped_.store(true);stop_.request_stop();}
ContactOwner::ContactOwner(std::shared_ptr<const storage::ProfileLease> lease):lease_(std::move(lease)){}
ContactOwner::~ContactOwner(){if(exchange_){exchange_->stop();}}
void ContactOwner::report(Error error){if(error.message.size()>256){error.message.resize(256);}failure_=std::move(error);}
std::expected<std::unique_ptr<ContactOwner>,Error> ContactOwner::open(const storage::ProfileLease& lease,unsigned index,std::optional<dht::Actor::Options>& options){
  // Original saved DHT=false has already projected no options. A cache never
  // opts a client into inbound/server/maintenance effects or creates a backend.
  if(!options||!options->inbound||!options->routingMaintenance){return std::unique_ptr<ContactOwner>{};}
  options->savedContacts.clear();auto retained=lease.retain();if(!retained){return std::unexpected(retained.error());}
  auto owner=std::unique_ptr<ContactOwner>(new ContactOwner(*retained));
  auto store=storage::I2pContacts::open(lease,index);
  if(!store){owner->report(store.error());return owner;}owner->store_=std::move(*store);
  auto snapshot=owner->store_->snapshot();if(!snapshot){owner->report(snapshot.error());return owner;}
  options->savedContacts=(*snapshot)->contacts;owner->writable_=(*snapshot)->writable;
  if(!owner->writable_){owner->report({Code::Unsupported,"Ambiguous original I2P contact cache is read-only"});return owner;}
  owner->exchange_=std::shared_ptr<ContactExchange>(new ContactExchange(*retained,index));return owner;
}
void ContactOwner::save(bool final){
  if(std::this_thread::get_id()!=worker_||!writable_||!exchange_||!store_){return;}
  const auto value=exchange_->latest_.load();
  if(!value||value->sequence<=handled_||(!final&&!value->periodic)){return;}
  if(!final&&exchange_->stopped_.load()){return;}handled_=value->sequence;
  const auto end=ContactExchange::Clock::now()+std::chrono::seconds(2);
  // Gate order is the existing OFD source gate, then the cache writer. No Qt
  // call/wait occurs here. Periodic snapshots require the exact captured epoch;
  // final post-join hints may span an accepted own checkpoint/publication.
  auto access=lease_->sourceAccess();if(!access){report(access.error());return;}
  if((!final&&access->revision()!=value->revision)||!value->source||!value->source->lease()||!value->source->lease()->sharesOwnership(*lease_)
      ||value->source->mixedMode()!=(store_->index()==0)
      ||value->identity!=value->source->destination().hash){report({Code::Conflict,"I2P contact export source epoch changed; snapshot skipped"});return;}
  // This validates captured filesystem/key/config bytes after an intentional
  // close/own checkpoint as well. Same retained source object, identity, key
  // and current cache CAS remain required; this never reactivates authority.
  auto valid=value->source->validateTransportSnapshot();if(!valid){report(valid.error());return;}
  auto saved=store_->save(value->rows,final?std::stop_token{}:exchange_->stop_.get_token(),end);
  if(!saved){report(saved.error());writable_=false;exchange_->stop();return;}
  valid=value->source->validateTransportSnapshot();if(!valid){report(valid.error());writable_=false;exchange_->stop();return;}failure_.reset();
}
void ContactOwner::poll(){
  if(finished_||std::this_thread::get_id()!=worker_){return;}
  try{if(exchange_&&exchange_->failed_.load()){writable_=false;report({Code::Io,"I2P contact snapshot capture failed; cache is read-only"});return;}save(false);}
  catch(...){writable_=false;if(exchange_){exchange_->stop();}report({Code::Io,"I2P contact worker publication failed"});}
}
void ContactOwner::finish(){
  if(finished_||std::this_thread::get_id()!=worker_){return;}if(exchange_){exchange_->stop();}try{save(true);}catch(...){report({Code::Io,"Final I2P contact snapshot skipped after publication failure"});}finished_=true;
}
std::optional<Error> ContactOwner::diagnostic()const{
  if(std::this_thread::get_id()!=worker_){return Error{Code::Unsupported,"I2P contact cache diagnostic requires its Host worker"};}return failure_;
}
}
