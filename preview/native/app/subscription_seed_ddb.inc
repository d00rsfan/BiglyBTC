// SPDX-License-Identifier: GPL-2.0-or-later
namespace {
const std::string& subscriptionSeedReadHandler() {
 // DDBaseHelpers preserves the Azureus transfer type's original class name.
 constexpr auto name=engine::SubscriptionSeedTransferType;
 static const auto handler=QCryptographicHash::hash(
     QByteArrayView(name.data(),static_cast<qsizetype>(name.size())),QCryptographicHash::Sha1).toStdString();
 return handler;
}

std::optional<std::int64_t> currentSubscriptionSeed(const Shared& shared,std::string_view lookup) {
 const auto catalog=shared.subscriptionSeeds.load();
 if(catalog&&Clock::now()<catalog->expires&&catalog->lookupKeys.contains(lookup)) {
  return catalog->creationSeconds;
 }
 return {};
}
}

void Owner::forgetLocalSubscriptionPresence(std::size_t lane,const engine::SubscriptionSeedHash& hash) {
 auto& records=localSubscriptionPresence_[lane];
 const auto at=records.find(hash);
 if (at==records.end()) { return; }
 if (lanes_[lane]&&lanes_[lane]->database) {
  PublicationClock clock;
  static_cast<void>(lanes_[lane]->database->removeLocal(at->second.key,0,clock));
 }
 subscriptionPresenceKeys_[lane].erase(at->second.key);
 records.erase(at);
}

bool Owner::subscriptionPresenceReadPermit(std::size_t lane,std::string_view key,
    biglybtc::core::MoveOnlyFunction<void(bool)> complete) {
 const auto hash=subscriptionPresenceKeys_[lane].find(key)->second;
 const auto catalog=shared_->subscriptionSeeds.load();
 const auto creation=catalog?catalog->creationSeconds:0;
 const auto lease=shared_->lease;
 const auto policy=lanes_[lane]->policy;
 auto accepted=std::make_shared<bool>(false);
 auto callback=std::make_shared<biglybtc::core::MoveOnlyFunction<void(bool)>>(std::move(complete));
 const auto end=Clock::now()+std::chrono::seconds(5);
 return gateWithSourceAccess(end,[this,lane,hash,accepted,callback] {
  const auto current=shared_->subscriptionSeeds.load();
  if (!*accepted||!current||Clock::now()>=current->expires
      ||!std::ranges::binary_search(current->publicationHashes,hash)) {
   forgetLocalSubscriptionPresence(lane,hash);
  }
  active_->operation->data.lanes[lane]=true;
  (*callback)(allowed(lane));
 },[lease,policy,hash,creation,accepted,end](std::stop_token stop,const storage::ProfileSourceAccess& access)->Broker::Result {
  if (stop.stop_requested()||Clock::now()>=end) {
   return std::unexpected(failure(storage::ErrorCode::Cancelled,"Subscription publication read expired"));
  }
  auto permitted=policy->validate();
  if (!permitted) { return std::unexpected(permitted.error()); }
  const auto seed=search::readSavedSubscriptionSeed(*lease,access,{hash.data(),hash.size()},
      search::SubscriptionSeedKey::Publication,search::SubscriptionSeedNetwork::Public,creation);
  *accepted=seed&&bool(*seed)&&(**seed).metainfo.presence.has_value();
  return Broker::Data{};
 },[callback]{(*callback)(false);});
}

std::vector<nodes::Contact> Owner::subscriptionDestinations(std::size_t lane,std::string_view key)const {
 auto& network=*lanes_[lane];
 const auto routing=network.actor->routingSnapshot();
 if (!routing || key.size()!=nodes::NodeId{}.size()) { return {}; }
 std::vector<nodes::Contact> contacts;
 for (std::size_t at=0;at<routing->contactCount;++at) {
  const auto& contact=routing->contacts[at].contact;
  const auto peer=peerOf(contact);
  if (contact.id!=network.local.id && (owned_?peer.address.isLoopback():publicAddress(peer))) {
   contacts.push_back(contact);
  }
 }
 std::ranges::sort(contacts,[&](const auto& left,const auto& right) {
  for (std::size_t index=0;index<key.size();++index) {
   const auto part=static_cast<unsigned char>(key[index]);
   const auto a=left.id[index]^part,b=right.id[index]^part;
   if (a!=b) { return a<b; }
  }
  return false;
 });
 if (contacts.size()>20) { contacts.resize(20); }
 return contacts;
}

bool Owner::storeSubscriptionPresence(std::size_t lane,std::string key,const services::azureus_values::Value& value) {
 const auto contacts=subscriptionDestinations(lane,key);
 if (contacts.empty()) { return false; }
 auto& network=*lanes_[lane];
 const auto published=std::make_shared<const services::azureus_values::Value>(value);
 const auto epoch=networkEpoch_;
 bool requested=false;
 for (const auto& contact:contacts) {
  const auto peer=peerOf(contact);
  const auto protocol=contact.protocol;
  const auto queued=network.actor->requestStoreToken(peer,protocol,this,
      [this,lane,epoch,key,peer,protocol,published](auto response) {
   if (closing_||epoch!=networkEpoch_||!response||!lanes_[lane]
       ||!subscriptionPresenceKeys_[lane].contains(key)) { return; }
   const auto* payload=std::get_if<udp::find::Decoded>(&*response);
   const auto* token=payload?std::get_if<udp::find::ReplyBody>(&payload->body()):nullptr;
   if (!token) { return; }
   const auto nonce=token->randomId;
   static_cast<void>(subscriptionPresenceReadPermit(lane,key,[this,lane,epoch,key,peer,protocol,nonce,published](bool permitted) {
    if (!permitted||closing_||epoch!=networkEpoch_||!lanes_[lane]
        ||!subscriptionPresenceKeys_[lane].contains(key)) { return; }
    udp::StoreRequest request;
    request.randomId=nonce;
    request.keys={key};
    request.valueSets={{*published}};
    static_cast<void>(lanes_[lane]->actor->store(peer,protocol,std::move(request),this,[](auto){},std::chrono::seconds(15)));
   }));
  },std::chrono::seconds(15));
  requested|=queued.has_value();
 }
 return requested;
}

void Owner::subscriptionPresenceTick() {
 const auto now=Clock::now();
 if (now<subscriptionPresenceNext_ || subscriptionPresencePending_) { return; }
 subscriptionPresenceNext_=now+std::chrono::seconds(1);
 const auto catalog=shared_->subscriptionSeeds.load();
 const bool current=catalog&&now<catalog->expires;
 for (std::size_t scan=0;scan<localSubscriptionPresence_.size();++scan) {
  const auto index=(subscriptionPresenceLane_+scan)%localSubscriptionPresence_.size();
  auto& records=localSubscriptionPresence_[index];
  auto& lane=lanes_[index];
  if (!lane || !lane->ready || !lane->actor || !lane->database) { continue; }
  for (auto entry=records.begin();entry!=records.end();) {
   if (current&&std::ranges::binary_search(catalog->publicationHashes,entry->first)) { ++entry;continue; }
   const auto hash=(entry++)->first;
   forgetLocalSubscriptionPresence(index,hash);
  }
  if (!current||catalog->publicationHashes.empty()) { continue; }
  auto& cursor=subscriptionPresenceCursor_[index];
  const auto count=catalog->publicationHashes.size();
  for (std::size_t probes=0;probes<count;++probes) {
   const auto hash=catalog->publicationHashes[cursor++%count];
   auto prior=records.find(hash);
   if (prior!=records.end()&&now<prior->second.refreshAfter) { continue; }
   const auto lease=shared_->lease;
   const auto policy=lane->policy;
   const auto versions=lane->versions;
   const auto creation=catalog->creationSeconds;
   const auto nonce=subscriptionPresenceNonce_;
   const auto end=now+std::chrono::seconds(5);
   struct PreparedPresence { std::string key,value;std::int32_t version=0; };
   auto prepared=std::make_shared<std::optional<PreparedPresence>>();
   const bool accepted=gateWithSourceAccess(end,[this,index,hash,prepared] {
    subscriptionPresencePending_=false;
    const auto selected=shared_->subscriptionSeeds.load();
    if (!*prepared || !selected || Clock::now()>=selected->expires
        || !std::ranges::binary_search(selected->publicationHashes,hash) || !lanes_[index]) {
     forgetLocalSubscriptionPresence(index,hash);
     return;
    }
    active_->operation->data.lanes[index]=true;
    if (!allowed(index)) { return; }
    auto& network=*lanes_[index];
    PublicationClock clock;
    auto& records=localSubscriptionPresence_[index];
    auto& keys=subscriptionPresenceKeys_[index];
    const auto& record=**prepared;
    const auto [entry,added]=records.try_emplace(hash,LocalSubscriptionPresence{record.key,{}});
    bool indexedNew=false;
    const auto discardUnpublished=qScopeGuard([&] {
     if (added && entry->second.refreshAfter==Clock::time_point{}) {
      if (indexedNew) { keys.erase(record.key); }
      records.erase(entry);
     }
    });
    if (entry->second.key!=record.key) { return; }
    const auto [indexed,fresh]=keys.try_emplace(record.key,hash);
    indexedNew=fresh;
    if (!fresh && indexed->second!=hash) { return; }
    const auto local=network.database->putLocal(record.key,record.value,record.version,clock);
    if (!local) { return; }
    entry->second.refreshAfter=Clock::now()+std::chrono::seconds(10);
    if (storeSubscriptionPresence(index,record.key,{local->version(),local->creationTime(),
        record.value,network.local,local->flags(),local->lifeTimeHours(),local->replicationControl()})) {
     entry->second.refreshAfter=Clock::now()+std::chrono::minutes(10);
    }
   },[lease,policy,versions,creation,nonce,hash,end,prepared](std::stop_token stop,
       const storage::ProfileSourceAccess& access)->Broker::Result {
    if (stop.stop_requested() || Clock::now()>=end) {
     return std::unexpected(failure(storage::ErrorCode::Cancelled,"Subscription publication expired"));
    }
    auto permitted=policy->validate();
    if (!permitted) { return std::unexpected(permitted.error()); }
    const auto seed=search::readSavedSubscriptionSeed(*lease,access,{hash.data(),hash.size()},
        search::SubscriptionSeedKey::Publication,search::SubscriptionSeedNetwork::Public,creation);
    if (!seed||!*seed||!(**seed).metainfo.presence) { return Broker::Data{}; }
    const auto& presence=*(**seed).metainfo.presence;
    auto value=search::encodeSubscriptionPresence(presence,nonce);
    if (!value) { return Broker::Data{}; }
    auto version=versions->next(QDateTime::currentMSecsSinceEpoch());
    if (!version) { return std::unexpected(version.error()); }
    const std::string key(presence.dhtKey.data(),presence.dhtKey.size());
    prepared->emplace(PreparedPresence{key,std::move(*value),*version});
    return Broker::Data{};
   },[this]{subscriptionPresencePending_=false;});
   if (accepted) {
    subscriptionPresencePending_=true;
    subscriptionPresenceLane_=(index+1)%localSubscriptionPresence_.size();
   }
   return;
  }
 }
}

bool Owner::associationCurrent(const AssociationLookup& job)const {
 const auto catalog=shared_->subscriptionSeeds.load();
 const auto now=Clock::now();
 return !closing_&&!resettingNetwork_&&job.epoch==networkEpoch_&&now<job.end&&catalog&&now<catalog->expires
     &&catalog->creationSeconds==job.creationSeconds&&std::ranges::binary_search(catalog->publicationHashes,job.source)
     &&lanes_[job.lane]&&lanes_[job.lane]->ready&&lanes_[job.lane]->actor;
}

void Owner::associationPermit(const std::shared_ptr<AssociationLookup>& job,core::MoveOnlyFunction<void()> effect,
    const std::shared_ptr<std::optional<std::int32_t>>& version) {
 if (!associationCurrent(*job)) { return; }
 const auto& lane=*lanes_[job->lane];
 const auto lease=shared_->lease;
 const auto end=std::min(job->end,Clock::now()+std::chrono::seconds(5));
 auto accepted=std::make_shared<bool>(false);
 static_cast<void>(gateWithSourceAccess(end,[this,job,accepted,effect=std::move(effect)]() mutable {
  if (!*accepted||!associationCurrent(*job)) { return; }
  active_->operation->data.lanes[job->lane]=true;
  if (allowed(job->lane)) { effect(); }
 },[lease,policy=lane.policy,versions=lane.versions,job,end,accepted,version](std::stop_token stop,
     const storage::ProfileSourceAccess& access)->Broker::Result {
  if (stop.stop_requested()||Clock::now()>=end) {
   return std::unexpected(failure(storage::ErrorCode::Cancelled,"Subscription association expired"));
  }
  auto permitted=policy->validate();
  if (!permitted) { return std::unexpected(permitted.error()); }
  const auto records=search::savedSubscriptionAssociationPublications(*lease,access,
      {job->source.data(),job->source.size()},search::SubscriptionSeedNetwork::Public,job->creationSeconds);
  if (!records||std::ranges::find(*records,job->record)==records->end()) { return Broker::Data{}; }
  if (version) {
   auto reserved=versions->next(QDateTime::currentMSecsSinceEpoch());
   if (!reserved) { return std::unexpected(reserved.error()); }
   version->emplace(*reserved);
  }
  *accepted=true;
  return Broker::Data{};
 }));
}

void Owner::publishSubscriptionAssociation(std::shared_ptr<AssociationLookup> job) {
 if (job->diversified||job->hits.size()>=10) { return; }
 auto version=std::make_shared<std::optional<std::int32_t>>();
 associationPermit(job,[this,job,version] {
  if (!*version) { return; }
  auto& network=*lanes_[job->lane];
  constexpr std::uint8_t anonymous=0x10,precious=0x20;
  const auto flags=static_cast<std::uint8_t>(anonymous|(job->hits.size()<3?precious:0));
  const std::string key(job->record.dhtKey.data(),job->record.dhtKey.size());
  const auto value=std::make_shared<const services::azureus_values::Value>(services::azureus_values::Value{
      **version,QDateTime::currentMSecsSinceEpoch(),{job->record.value.data(),job->record.value.size()},network.local,flags,0,-1});
  for (const auto& contact:job->destinations) {
   const auto peer=peerOf(contact);
   const auto protocol=contact.protocol;
   static_cast<void>(network.actor->requestStoreToken(peer,protocol,this,[this,job,key,value,peer,protocol](auto response) {
    if (!response||!associationCurrent(*job)) { return; }
    const auto* payload=std::get_if<udp::find::Decoded>(&*response);
    const auto* token=payload?std::get_if<udp::find::ReplyBody>(&payload->body()):nullptr;
    if (!token) { return; }
    associationPermit(job,[this,job,key,value,peer,protocol,nonce=token->randomId] {
     udp::StoreRequest request;
     request.randomId=nonce;
     request.keys={key};
     request.valueSets={{*value}};
     static_cast<void>(lanes_[job->lane]->actor->store(peer,protocol,std::move(request),this,[](auto){},std::chrono::seconds(10)));
    });
   },std::chrono::seconds(8)));
  }
 },version);
}

void Owner::lookupSubscriptionAssociation(std::shared_ptr<AssociationLookup> job) {
 const std::string_view key(job->record.dhtKey.data(),job->record.dhtKey.size());
 job->destinations=subscriptionDestinations(job->lane,key);
 nodes::NodeId id{};
 std::ranges::copy(key,id.begin());
 associationLookup_=job;
 auto& actor=*lanes_[job->lane]->actor;
 for (const auto& contact:job->destinations) {
  const auto queued=actor.findValue(peerOf(contact),contact.protocol,id,0,30,this,[this,job](auto response) {
   if (associationLookup_!=job||!associationCurrent(*job)) { return; }
   if (response) {
    const auto* reply=std::get_if<udp::lookup::ReplyBody>(&response->body());
    const auto* values=reply?std::get_if<udp::lookup::FoundValues>(&reply->result):nullptr;
    if (values) {
     job->diversified=job->diversified||values->diversification!=1;
     const std::string_view sid(job->record.value.data()+4,10);
     for (const auto& value:values->values) {
      if (job->hits.size()>=10) { break; }
      if (value.bytes.size()==job->record.value.size()&&std::string_view(value.bytes).substr(4)==sid) {
       job->hits.emplace(value.originator.id,value.bytes);
      }
     }
    }
    if (reply&&reply->continuation) { return; }
   }
   if (--job->pending==0) {
    associationLookup_.reset();
    publishSubscriptionAssociation(job);
   }
  },std::chrono::seconds(8));
  if (queued) { ++job->pending; }
 }
 if (!job->pending) { associationLookup_.reset(); }
}

void Owner::subscriptionAssociationTick() {
 const auto now=Clock::now();
 if (associationLookup_&&!associationCurrent(*associationLookup_)) { associationLookup_.reset(); }
 if (now<associationNext_||associationPreparing_||associationLookup_) { return; }
 associationNext_=now+std::chrono::seconds(1);
 const auto catalog=shared_->subscriptionSeeds.load();
 const bool current=catalog&&now<catalog->expires;
 for (std::size_t scan=0;scan<associationPositions_.size();++scan) {
  const auto lane=(associationLane_+scan)%associationPositions_.size();
  auto& positions=associationPositions_[lane];
  std::erase_if(positions,[&](const auto& item) {
   return !current||!std::ranges::binary_search(catalog->publicationHashes,item.first);
  });
  if (!current||catalog->publicationHashes.empty()||!lanes_[lane]||!lanes_[lane]->ready||!lanes_[lane]->actor) { continue; }
  const auto count=catalog->publicationHashes.size();
  for (std::size_t at=0;at<count;++at) {
   const auto hash=catalog->publicationHashes[associationCursors_[lane]++%count];
   auto& position=positions[hash];
   if (now<position.retry) { continue; }
   auto job=std::make_shared<AssociationLookup>();
   job->source=hash;job->lane=lane;job->epoch=networkEpoch_;job->end=now+std::chrono::seconds(30);
   job->creationSeconds=catalog->creationSeconds;
   auto selected=std::make_shared<std::optional<search::SavedSubscriptionAssociationPublication>>();
   const auto lease=shared_->lease;
   const auto policy=lanes_[lane]->policy;
   const bool queued=gateWithSourceAccess(now+std::chrono::seconds(5),[this,job,selected] {
    associationPreparing_=false;
    if (!associationCurrent(*job)) { return; }
    auto& position=associationPositions_[job->lane][job->source];
    if (!*selected) {
     position={0,Clock::now()+(position.next?std::chrono::minutes(10):std::chrono::seconds(30))};
     return;
    }
    active_->operation->data.lanes[job->lane]=true;
    if (allowed(job->lane)) {
     job->record=**selected;
     lookupSubscriptionAssociation(job);
     if (associationLookup_==job) { ++position.next; }
    }
   },[lease,policy,hash,offset=position.next,creation=catalog->creationSeconds,selected](std::stop_token stop,
       const storage::ProfileSourceAccess& access)->Broker::Result {
    if (stop.stop_requested()) { return std::unexpected(failure(storage::ErrorCode::Cancelled,"Subscription association cancelled")); }
    auto permitted=policy->validate();
    if (!permitted) { return std::unexpected(permitted.error()); }
    const auto records=search::savedSubscriptionAssociationPublications(*lease,access,{hash.data(),hash.size()},
        search::SubscriptionSeedNetwork::Public,creation);
    if (records&&offset<records->size()) { selected->emplace(records->at(offset)); }
    return Broker::Data{};
   },[this]{associationPreparing_=false;});
   if (queued) {
    associationPreparing_=true;
    associationLane_=(lane+1)%associationPositions_.size();
   }
   return;
  }
 }
}

void Owner::registerSubscriptionSeedRead(std::size_t lane) {
 static_cast<void>(lanes_[lane]->actor->registerTransferHandler(subscriptionSeedReadHandler(),false,this,
     [this,lane](auto request){receiveSubscriptionSeedRead(lane,std::move(request));},
     [this,lane]()->std::optional<std::uint64_t>{
       if(!closing_&&allowed(lane)) { return networkEpoch_; }
       return {};
     },udp::TransferMode::Read));
}

void Owner::receiveSubscriptionSeedRead(std::size_t lane,std::shared_ptr<const udp::TransferRequest> request) {
 if(closing_||!request||!lanes_[lane]||!lanes_[lane]->ready||request->sourceRevision!=networkEpoch_
     ||request->request.size()!=20) { return; }
 const auto creation=currentSubscriptionSeed(*shared_,request->request);
 if(!creation) { return; }
 const auto end=std::min(request->deadline,Clock::now()+std::chrono::seconds(5));
 const auto policy=lanes_[lane]->policy;
 const auto lease=shared_->lease;
 auto reply=std::make_shared<std::optional<std::string>>();
 gateWithSourceAccess(end,[this,lane,request,reply,creation]{
  if(!*reply||request->sourceRevision!=networkEpoch_
      ||currentSubscriptionSeed(*shared_,request->request)!=creation) { return; }
  active_->operation->data.lanes[lane]=true;
  if(allowed(lane)) { static_cast<void>(lanes_[lane]->actor->answerTransfer(request->receipt,std::move(**reply))); }
 },[policy,lease,request,reply,end,creation=*creation](std::stop_token stop,
     const storage::ProfileSourceAccess& access)->Broker::Result {
  if(stop.stop_requested()||Clock::now()>=end) {
   return std::unexpected(failure(storage::ErrorCode::Cancelled,"Subscription read expired"));
  }
  auto permitted=policy->validate();
  if(!permitted) { return std::unexpected(permitted.error()); }
  const auto seed=search::readSavedSubscriptionSeed(*lease,access,request->request,search::SubscriptionSeedKey::DdbLookup,
      search::SubscriptionSeedNetwork::Public,creation);
  if(seed&&*seed) {
   auto encoded=search::subscriptionSeedDdbResponse((**seed).metainfo);
   if(encoded) { reply->emplace(std::move(*encoded)); }
  }
  return Broker::Data{};
 });
}
