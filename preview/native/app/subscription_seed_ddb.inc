// SPDX-License-Identifier: GPL-2.0-or-later
namespace {
const std::string& subscriptionSeedReadHandler() {
 // DDBaseHelpers preserves the Azureus transfer type's original class name.
 static const auto handler=QCryptographicHash::hash(
     "org.gudy.azureus2.pluginsimpl.local.ddb.DDBaseTTTorrent",QCryptographicHash::Sha1).toStdString();
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
