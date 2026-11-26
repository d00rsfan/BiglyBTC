// SPDX-License-Identifier: GPL-2.0-or-later
#include "directory.hpp"
#include "biglybtc/move_only_function.hpp"
#include <biglybtc/azureus_endpoint.hpp>
#include <QCryptographicHash>
#include <QDateTime>
#include <QRandomGenerator>
#include <QThread>
#include <limits>
#include <algorithm>
#include <utility>
namespace biglybtc::services::azureus_nat {
namespace {
namespace ctl=azureus_control;
transport::Peer peer(const udp::Address& a){QHostAddress ip;if(a.length==4){ip=QHostAddress((quint32(a.bytes[0])<<24)|(quint32(a.bytes[1])<<16)|(quint32(a.bytes[2])<<8)|a.bytes[3]);}else{Q_IPV6ADDR v{};std::copy_n(a.bytes.begin(),16,v.c);ip=QHostAddress(v);}return {ip,a.port};}
struct Wall final:azureus_db_value::WallClock{std::int64_t nowMilliseconds()noexcept override{return QDateTime::currentMSecsSinceEpoch();}};
}
Milliseconds Directory::now(){return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count();}
Milliseconds Directory::cacheNow()const{return cacheClock_?cacheClock_():now();}
nodes::NodeId Directory::key(const nodes::Contact& c){auto raw=publishKey(c.id).value();const auto digest=QCryptographicHash::hash(QByteArray::fromStdString(raw),QCryptographicHash::Sha1);nodes::NodeId out{};std::copy_n(reinterpret_cast<const std::uint8_t*>(digest.data()),20,out.begin());return out;}
Directory::Directory(transport::OwnedPingTransport& lane,nodes::Contact local,std::shared_ptr<azureus_db_runtime::Owner> db,std::uint64_t epoch,SessionHooks hooks,bool owned,std::function<Milliseconds()> clock,Remember remember,std::shared_ptr<core::AzureusDiversificationLedger> ledger):lane_(&lane),local_(std::move(local)),database_(std::move(db)),epoch_(epoch),hooks_(std::move(hooks)),owned_(owned),cacheClock_(std::move(clock)),remember_(std::move(remember)),diversifications_(ledger?std::move(ledger):std::make_shared<core::AzureusDiversificationLedger>()){
 timer_.setInterval(1000);QObject::connect(&timer_,&QTimer::timeout,this,[this]{tick();});
}
Result<std::unique_ptr<Directory>> Directory::open(transport::OwnedPingTransport& lane,nodes::Contact local,std::shared_ptr<azureus_db_runtime::Owner> db,std::uint64_t epoch,SessionHooks hooks,bool owned,std::function<Milliseconds()> clock,Remember remember,std::shared_ptr<core::AzureusDiversificationLedger> ledger){
 if(lane.thread()!=QThread::currentThread()||!db||!hooks.afterRelease||!hooks.withPermit||!hooks.revision){return std::unexpected(Error::Input);}auto bound=db->localContact();if(!bound||!*bound||(**bound).id!=local.id||(**bound).address!=local.address){return std::unexpected(Error::Input);}
 auto self=std::unique_ptr<Directory>(new Directory(lane,std::move(local),std::move(db),epoch,std::move(hooks),owned,std::move(clock),std::move(remember),std::move(ledger)));if(!self->valid(self->local_)||self->cacheNow()<0){return std::unexpected(Error::Input);}self->timer_.start();return self;
}
Directory::~Directory(){close();}
bool Directory::valid(const nodes::Contact& c)const{return c.address.port&&c.address.length==local_.address.length&&c.protocol>=51&&c.protocol<=55&&(owned_?peer(c.address).address.isLoopback():core::azureusPublicAddress(std::span(c.address.bytes).first(c.address.length)));}
bool Directory::guard(Clock::time_point end,biglybtc::core::MoveOnlyFunction<void()> fn){
 if(closed_||!lane_||QThread::currentThread()!=thread()||Clock::now()>=end){return false;}QPointer<Directory> self(this);auto queued=hooks_.withPermit(end,[self,end,fn=std::move(fn)]()mutable{if(!self||self->closed_){return;}if(!self->lane_||self->hooks_.revision()!=std::optional{self->epoch_}){self->shutdown(DirectoryError::Source);return;}if(Clock::now()<end){fn();}});if(!queued){shutdown(DirectoryError::Source);}return queued;
}
Result<std::uint64_t> Directory::read(RoutedContact subject,QObject* receiver,DirectoryCallback callback,std::span<const nodes::Contact> seeds){
 if(closed_||!lane_||QThread::currentThread()!=thread()||!receiver||receiver->thread()!=thread()||!callback||!valid(subject.external())||serial_>=std::numeric_limits<std::uint64_t>::max()-1||seeds.size()>20){return std::unexpected(Error::Input);}
 const auto name=socketAddress(subject.external().address).value();const auto n=cacheNow();if(n<0){return std::unexpected(Error::Input);}auto at=cacheEntries_.find(name);std::shared_ptr<Work> work;bool cached=false;
 if(at!=cacheEntries_.end()){auto old=at->second;if(!old->finished||(n>=old->completed&&static_cast<std::uint64_t>(n)-static_cast<std::uint64_t>(old->completed)<120000)){work=old;cached=old->finished;}else{cacheEntries_.erase(at);}}
 if(work&&work->waiters.size()>=16){return std::unexpected(Error::Limit);}
 if(!work){if(facts().active>=8){return std::unexpected(Error::Limit);}if(cacheEntries_.size()>=128){auto oldest=cacheEntries_.end();for(auto i=cacheEntries_.begin();i!=cacheEntries_.end();++i){if(i->second->finished&&(oldest==cacheEntries_.end()||i->second->completed<oldest->second->completed)){oldest=i;}}if(oldest==cacheEntries_.end()){return std::unexpected(Error::Limit);}cacheEntries_.erase(oldest);}work=std::make_shared<Work>();work->id=++serial_;work->subject=subject.external();work->key=key(work->subject);work->end=work->lookupEnd=Clock::now()+std::chrono::seconds(20);cacheEntries_[name]=work;works_.push_back(work);}
 const auto id=++serial_;work->waiters.push_back({id,receiver,std::move(callback)});++reads_;if(cached){if(std::ranges::find(works_,work)==works_.end()){works_.push_back(work);}++hits_;guard(Clock::now()+std::chrono::seconds(1),[this,work]{finish(work);});}else if(work->control.state()==ctl::LookupState::Idle){guard(work->end,[this,work,seeds=std::vector<nodes::Contact>(seeds.begin(),seeds.end())]()mutable{begin(work,std::move(seeds));});}return id;
}
void Directory::begin(const std::shared_ptr<Work>& work,std::vector<nodes::Contact> seeds){
 if(closed_||work->finished||work->control.state()!=ctl::LookupState::Idle){return;}
 if(work->visited.empty()){auto known=diversifications_->find(work->key,QDateTime::currentMSecsSinceEpoch());if(known){work->visited.insert(work->key);diversify(work,known->type);if(!work->publishing&&!work->pendingKeys.empty()){const auto next=work->pendingKeys.front();work->pendingKeys.pop_front();work->key=next.first;work->depth=next.second;}}}work->visited.insert(work->key);if(seeds.empty()){auto routing=lane_->routingSnapshot();if(routing){for(std::size_t i=0;i<routing->contactCount;++i){seeds.push_back(routing->contacts[i].contact);}}}
 std::erase_if(seeds,[this](const auto& c){return !valid(c)||c.id==local_.id;});std::ranges::sort(seeds,[&](const auto& a,const auto& b){for(std::size_t i=0;i<20;++i){const auto aa=a.id[i]^work->key[i],bb=b.id[i]^work->key[i];if(aa!=bb){return aa<bb;}}return false;});if(seeds.size()>20){seeds.resize(20);}std::vector<ctl::AdmittedContact> admitted;for(const auto& c:seeds){if(lane_->attachKnownPeer(peer(c.address),c.protocol,work->lookupEnd)){admitted.push_back({c,true});}}
 ctl::LookupOptions options;options.localId=local_.id;options.target=work->key;options.timeoutMillis=std::max<Milliseconds>(1,std::chrono::duration_cast<std::chrono::milliseconds>(work->lookupEnd-Clock::now()).count());options.maximumValues=1;options.maximumContacts=owned_?ctl::LookupController::MaximumOwnedContacts:ctl::LookupController::MaximumContacts;options.kind=work->publishing?ctl::LookupKind::Node:ctl::LookupKind::Value;options.addressScope=owned_?ctl::LookupAddressScope::OwnedLoopback:(work->publishing?ctl::LookupAddressScope::PublicNode:ctl::LookupAddressScope::PublicValue);
 if(!work->control.start(epoch_,work->id,options,admitted,now())){finish(work,DirectoryError::Input);return;}pump(work);
}
void Directory::pump(const std::shared_ptr<Work>& work){
 if(closed_||work->finished||work->lookupDone){return;}auto intents=work->control.takeIntents(now());if(!intents){finish(work,DirectoryError::Input);return;}QPointer<Directory> self(this);
 for(const auto& intent:*intents){if(!lane_->attachKnownPeer(peer(intent.contact.address),intent.contact.protocol,work->lookupEnd)){static_cast<void>(work->control.finish(intent.node,ctl::LookupNodeState::Rejected,now()));continue;}const auto budget=std::min(std::chrono::milliseconds(15000),std::chrono::duration_cast<std::chrono::milliseconds>(work->lookupEnd-Clock::now()));if(budget.count()<=0){static_cast<void>(work->control.finish(intent.node,ctl::LookupNodeState::Timeout,now()));continue;}
  std::expected<transport::Ticket,transport::Failure> ticket=std::unexpected(transport::Failure::State);
  if(work->publishing){ticket=lane_->findNode(peer(intent.contact.address),intent.contact.protocol,intent.target,this,[self,work,node=intent.node,key=work->key](auto result){if(self&&work->key==key){self->nodeReply(work,node,std::move(result));}},budget);}else{ticket=lane_->findValue(peer(intent.contact.address),intent.contact.protocol,intent.target,0,1,this,[self,work,node=intent.node,key=work->key](auto result){if(self&&work->key==key){self->valueReply(work,node,std::move(result));}},budget);}
  if(ticket){work->tickets[intent.node]=*ticket;static_cast<void>(work->control.sent(intent.node,ticket->value,now()));}else{static_cast<void>(work->control.finish(intent.node,ctl::LookupNodeState::Failed,now()));}
 }
 if(auto cancel=work->control.cancellationIntents()){for(const auto& intent:*cancel){auto at=work->tickets.find(intent.node);if(at!=work->tickets.end()){lane_->cancel(at->second);}}}
 if(ctl::lookupTerminal(work->control.state())){work->lookupDone=true;if(work->publishing){publishStores(work);}else{nextKey(work);}}
}
void Directory::valueReply(const std::shared_ptr<Work>& work,std::uint64_t node,transport::FindValueOutcome outcome){
 if(closed_||work->finished){return;}guard(work->end,[this,work,node,key=work->key,outcome=std::move(outcome)]()mutable{if(work->finished||work->key!=key){return;}if(!outcome){static_cast<void>(work->control.finish(node,ctl::LookupNodeState::Timeout,now()));work->tickets.erase(node);}else{
  auto* body=std::get_if<transport::lookup::ReplyBody>(&outcome->body());if(!body){static_cast<void>(work->control.finish(node,ctl::LookupNodeState::Failed,now()));}
  else if(auto* values=std::get_if<transport::lookup::FoundValues>(&body->result)){diversify(work,values->diversification);static_cast<void>(work->control.values(node,values->values,body->continuation,values->diversification,now()));for(const auto& v:values->values){if(v.originator.id!=work->subject.id){continue;}auto relay=decodePublishValue(v.bytes,cache_);if(relay&&valid(*relay)){auto route=RoutedContact::observed(*relay,relay->address,cache_);if(route){work->result=std::move(*route);finish(work);return;}}}}
  else if(auto* contacts=std::get_if<transport::lookup::FoundContacts>(&body->result)){std::vector<ctl::AdmittedContact> accepted;for(const auto& c:contacts->contacts){if(valid(c)&&lane_->attachKnownPeer(peer(c.address),c.protocol,work->lookupEnd)){accepted.push_back({c,true});}}static_cast<void>(work->control.contacts(node,accepted,body->continuation,now()));}
  if(body&&!body->continuation){work->tickets.erase(node);}
 }pump(work);});
}
void Directory::nodeReply(const std::shared_ptr<Work>& work,std::uint64_t node,transport::FindNodeOutcome outcome){
 if(closed_||work->finished){return;}guard(work->end,[this,work,node,key=work->key,outcome=std::move(outcome)]()mutable{if(work->finished||work->key!=key){return;}if(!outcome){static_cast<void>(work->control.finish(node,ctl::LookupNodeState::Timeout,now()));}else{const auto* reply=std::get_if<transport::find::ReplyBody>(&outcome->body());if(!reply){static_cast<void>(work->control.finish(node,ctl::LookupNodeState::Failed,now()));}else{work->tokens[node]=reply->randomId;std::vector<ctl::AdmittedContact> contacts;for(const auto& c:reply->contacts){if(valid(c)&&lane_->attachKnownPeer(peer(c.address),c.protocol,work->lookupEnd)){contacts.push_back({c,true});}}static_cast<void>(work->control.contacts(node,contacts,false,now()));}}work->tickets.erase(node);pump(work);});
}
bool Directory::publication(std::optional<RoutedContact> relay,std::int32_t version,std::span<const nodes::Contact> seeds){
 if(closed_||!lane_||QThread::currentThread()!=thread()||version<0||seeds.size()>20||serial_==std::numeric_limits<std::uint64_t>::max()||(relay&&!valid(relay->external()))){return false;}
 auto work=std::make_shared<Work>();work->id=++serial_;work->subject=local_;work->key=key(local_);work->publishing=true;work->lookupEnd=Clock::now()+std::chrono::seconds(20);work->end=Clock::now()+std::chrono::seconds(40);works_.push_back(work);
 return guard(work->end,[this,work,relay=std::move(relay),version,seeds=std::vector<nodes::Contact>(seeds.begin(),seeds.end())]()mutable{Wall clock;const std::string raw(reinterpret_cast<const char*>(work->key.data()),20);std::optional<azureus_db_value::DbValue> local;
  if(relay){auto encoded=encodePublishValue(relay->exportSnapshot());if(!encoded){finish(work,DirectoryError::Input);return;}auto stored=database_->putLocal(raw,*encoded,version,clock);if(!stored){finish(work,DirectoryError::Input);return;}local=std::move(*stored);}else{auto removed=database_->removeLocal(raw,version,clock);if(!removed||!*removed){finish(work);return;}local=std::move(**removed);}
  work->publication=azureus_values::Value{local->version(),local->creationTime(),std::string(local->payload().view().value_or(std::string_view{})),local_,local->flags(),local->lifeTimeHours(),local->replicationControl()};if(publication_&&!publication_->finished){finish(publication_,DirectoryError::Cancelled);}publication_=work;++publications_;begin(work,std::move(seeds));
 });
}
bool Directory::publish(RoutedContact relay,std::int32_t version,std::span<const nodes::Contact> seeds){return publication(std::move(relay),version,seeds);}
bool Directory::withdraw(std::int32_t version,std::span<const nodes::Contact> seeds){return publication({},version,seeds);}
void Directory::publishStores(const std::shared_ptr<Work>& work){
 if(work!=publication_||!work->publication){finish(work,DirectoryError::Cancelled);return;}auto snapshot=work->control.snapshot();if(!snapshot){finish(work,DirectoryError::Input);return;}std::vector<ctl::LookupNode> candidates;for(const auto& n:snapshot->nodes){if(n.state==ctl::LookupNodeState::Replied&&work->tokens.contains(n.id)){candidates.push_back(n);}}std::ranges::sort(candidates,[&](const auto& a,const auto& b){for(std::size_t i=0;i<20;++i){auto aa=a.contact.id[i]^work->key[i],bb=b.contact.id[i]^work->key[i];if(aa!=bb){return aa<bb;}}return false;});if(candidates.size()>20){candidates.resize(20);}QPointer<Directory> self(this);
 for(const auto& n:candidates){transport::StoreRequest request;request.randomId=work->tokens.at(n.id);request.keys={std::string(reinterpret_cast<const char*>(work->key.data()),20)};request.valueSets={{*work->publication}};auto id=std::make_shared<transport::Ticket>();auto ticket=lane_->store(peer(n.contact.address),n.contact.protocol,std::move(request),this,[self,work,id](auto result){if(!self||work->finished){return;}self->guard(work->end,[self,work,id,result=std::move(result)]{if(work->finished){return;}std::erase(work->stores,*id);if(result){++self->stores_;if(const auto* decoded=std::get_if<transport::storage::Decoded>(&*result)){if(const auto* reply=std::get_if<transport::storage::ReplyBody>(&decoded->body());reply&&reply->diversification&&!reply->diversification->empty()){self->diversify(work,std::int8_t(reply->diversification->front()));}}}if(work->stores.empty()){self->nextKey(work);}});},std::min(std::chrono::milliseconds(15000),std::chrono::duration_cast<std::chrono::milliseconds>(work->end-Clock::now())));if(ticket){*id=*ticket;work->stores.push_back(*ticket);}}
 if(work->stores.empty()){nextKey(work);}
}
bool Directory::restoreDiversifications(std::span<const core::AzureusRemoteDiversification> records,std::int64_t wall){
 if(closed_||QThread::currentThread()!=thread()||hooks_.revision()!=std::optional{epoch_}||wall<0){return false;}return diversifications_->restore(records,wall);
}
void Directory::diversify(const std::shared_ptr<Work>& work,std::int8_t type){
 if((type!=2&&type!=3)||work->depth>=4||work->visited.size()+work->pendingKeys.size()>=32){return;}const auto current=QDateTime::currentMSecsSinceEpoch();auto found=diversifications_->find(work->key,current);
 if(!found){const unsigned first=QRandomGenerator::system()->bounded(10U),second=(first+1+QRandomGenerator::system()->bounded(9U))%10;const auto ttl=172800000+QRandomGenerator::system()->bounded(86400000U);found=diversifications_->remember({work->key,type,current+ttl,{first,second}},current,!remember_);if(!found){return;}}
 if(remember_&&!diversifications_->persisted(*found)&&!work->persisting){work->persisting=true;const auto record=*found;QPointer<Directory> self(this);remember_(record,[self,work,record](bool saved){if(!self){return;}QMetaObject::invokeMethod(self,[self,work,record,saved]{if(!self||work->finished){return;}self->guard(work->end,[self,work,record,saved]{work->persisting=false;if(!saved||!self->diversifications_->markPersisted(record)){self->finish(work,DirectoryError::Source);return;}if(work->lookupDone&&work->stores.empty()){self->nextKey(work);}});},Qt::QueuedConnection);});}
 const unsigned first=QRandomGenerator::system()->bounded(10U),second=(first+1+QRandomGenerator::system()->bounded(9U))%10;const auto offsets=core::azureusDiversificationOffsets(*found,work->publishing,false,{first,second});
 for(auto offset:offsets){if(work->visited.size()+work->pendingKeys.size()>=32){break;}QByteArray raw(reinterpret_cast<const char*>(work->key.data()),20);raw.append(char(offset));const auto hash=QCryptographicHash::hash(raw,QCryptographicHash::Sha1);nodes::NodeId next{};std::copy_n(reinterpret_cast<const std::uint8_t*>(hash.data()),20,next.begin());if(work->visited.contains(next)||std::ranges::any_of(work->pendingKeys,[&](const auto& e){return e.first==next;})){continue;}work->pendingKeys.push_back({next,work->depth+1});}
}

void Directory::nextKey(const std::shared_ptr<Work>& work){
 if(work->finished||work->persisting){return;}if(work->pendingKeys.empty()||Clock::now()>=work->lookupEnd){finish(work);return;}std::vector<nodes::Contact> seeds;if(auto snapshot=work->control.snapshot()){for(const auto& node:snapshot->nodes){seeds.push_back(node.contact);}}for(const auto& [node,ticket]:work->tickets){(void)node;lane_->cancel(ticket);}work->tickets.clear();work->tokens.clear();const auto next=work->pendingKeys.front();work->pendingKeys.pop_front();work->key=next.first;work->depth=next.second;work->lookupDone=false;work->control=azureus_control::LookupController{};begin(work,std::move(seeds));
}
void Directory::finish(const std::shared_ptr<Work>& work,std::optional<DirectoryError> error){
 if(!work->finished){work->finished=true;work->completed=cacheNow();if(lane_){for(const auto& [node,ticket]:work->tickets){(void)node;lane_->cancel(ticket);}for(auto ticket:work->stores){lane_->cancel(ticket);}}work->tickets.clear();work->stores.clear();}
 if(error&&!work->publishing){auto name=socketAddress(work->subject.address).value();auto at=cacheEntries_.find(name);if(at!=cacheEntries_.end()&&at->second==work){cacheEntries_.erase(at);}}
 auto waiters=std::exchange(work->waiters,{});auto result=error?DirectoryOutcome(std::unexpected(*error)):DirectoryOutcome(work->result);
 if(!waiters.empty()){QPointer<Directory> self(this);auto deliver=[self,waiters=std::move(waiters),result=std::move(result)]()mutable{if(!self){return;}if(self->closed_){result=std::unexpected(self->closedReason_);}for(auto& waiter:waiters){if(waiter.receiver&&waiter.callback){QMetaObject::invokeMethod(self,[self,receiver=waiter.receiver,callback=std::move(waiter.callback),result]()mutable{if(self&&receiver){if(self->closed_&&result){result=std::unexpected(self->closedReason_);}callback(std::move(result));}},Qt::QueuedConnection);}}};if(!hooks_.afterRelease(std::move(deliver))){/* No effect may escape a withdrawn owner. */}}
}
bool Directory::cancel(std::uint64_t id){if(closed_||QThread::currentThread()!=thread()){return false;}for(const auto& work:works_){auto at=std::ranges::find_if(work->waiters,[&](const auto& w){return w.id==id;});if(at!=work->waiters.end()){auto waiter=std::move(*at);work->waiters.erase(at);if(waiter.receiver){QMetaObject::invokeMethod(this,[callback=std::move(waiter.callback)]()mutable{callback(std::unexpected(DirectoryError::Cancelled));},Qt::QueuedConnection);}if(work->waiters.empty()&&!work->finished){finish(work,DirectoryError::Cancelled);}return true;}}return false;}
DirectoryFacts Directory::facts()const{DirectoryFacts out;for(const auto& w:works_){out.active+=!w->finished;}for(const auto& [name,w]:cacheEntries_){(void)name;out.cached+=w->finished;}out.reads=reads_;out.hits=hits_;out.publications=publications_;out.stores=stores_;out.closed=closed_;return out;}
void Directory::tick(){if(closed_){return;}std::erase_if(works_,[](const auto& w){return w->finished&&w->waiters.empty();});auto works=works_;for(const auto& w:works){if(w->finished){continue;}guard(Clock::now()+std::chrono::seconds(1),[this,w]{if(Clock::now()>=w->end){finish(w);return;}if(w->lookupDone){return;}pump(w);});}}
void Directory::shutdown(DirectoryError reason){if(closed_){return;}closed_=true;closedReason_=reason;timer_.stop();const auto works=works_;for(const auto& w:works){if(!w->finished||!w->waiters.empty()){finish(w,reason);}}cacheEntries_.clear();works_.clear();publication_.reset();}
void Directory::close(){shutdown(DirectoryError::Closed);}
}
