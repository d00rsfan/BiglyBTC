// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "owned_i2p_service.hpp"
#include "persistent_session.hpp"
#include "network_policy.hpp"
#include "queue_positions.hpp"
#include "library_view.hpp"
#include <algorithm>
#include <map>
#include <set>
#include <tuple>

namespace biglybtc::app {
// Worker-confined. There is one original queue evaluation over both actual
// owners. Snapshot tokens are translated back to admission-bound effects;
// release acknowledgements from BOTH owners precede any replacement Run.
class CoexistCoordinator final {
  using Error=storage::Error;
  using Binding=engine::QueueBinding;
  using Decision=engine::ExternalQueueDecision;
  using Key=std::tuple<bool,std::string,std::uint64_t>;
  core::QueuePolicy policy_;
  std::map<Key,std::uint64_t> tokens_;
  std::uint64_t nextToken_=0;
  std::optional<core::SessionRateLimits> publicLimits_,privateLimits_;
  std::pair<const OwnedI2pService*,std::uint64_t> privateOwner_{};
  std::vector<std::tuple<Key,std::uint64_t,bool>> lastPositions_;
  std::optional<Error> positionFailure_;
  bool refreshPositions_=true;
  static std::expected<void,Error> fail(std::string message){return std::unexpected(Error{storage::ErrorCode::Conflict,std::move(message)});}
  static std::optional<std::int64_t> sum(std::optional<std::int64_t> a,std::optional<std::int64_t> b){
    if(!a||!b||*a<0||*b<0||*a>INT64_MAX-*b){return {}; }return *a+*b;
  }
  static core::SessionRateLimits limits(int down,int up){core::SessionRateLimits v;v.download=core::effectiveSessionRateLimit(down);v.upload=core::effectiveSessionRateLimit(up);return v;}
  std::expected<void,Error> rates(engine::PersistentSession& pub,OwnedI2pService* priv,
      const engine::NetworkPolicy& policy,bool seedingOnly,bool publicDemand,bool privateDemand){
    const auto owner=std::pair{priv,priv?priv->observation().generation:std::uint64_t{0}};
    if(privateOwner_!=owner){privateLimits_.reset();privateOwner_=owner;}
    const int down=policy.downloadBytesPerSecond;
    const int up=policy.seedingOnlyUpload&&seedingOnly?policy.seedingUploadBytesPerSecond:policy.uploadBytesPerSecond;
    if(priv&&((down>0&&down<2)||(up>0&&up<2))){return fail("The shared bandwidth budget is too small for two retained sessions");}
    const auto share=[&](int total,bool own,bool other){
      if(!total||!priv){return total;}
      if(own&&!other){return total-1;}if(!own&&other){return 1;}
      return total/2;
    };
    const auto p=limits(share(down,publicDemand,privateDemand),share(up,publicDemand,privateDemand));
    const auto q=limits(down?down-static_cast<int>(p.download.bytesPerSecond):0,up?up-static_cast<int>(p.upload.bytesPerSecond):0);
    const auto reduction=[](const std::optional<core::SessionRateLimits>& before,const core::SessionRateLimits& after){
      auto result=after;if(!before){return result;}
      const auto lower=[](core::EffectiveRateLimit a,core::EffectiveRateLimit b){
        if(a.state==core::RateLimitState::Unlimited){return b;}if(b.state==core::RateLimitState::Unlimited){return a;}return a.bytesPerSecond<b.bytesPerSecond?a:b;
      };result.download=lower(before->download,after.download);result.upload=lower(before->upload,after.upload);return result;
    };
    const auto applyPub=[&](const core::SessionRateLimits& v)->std::expected<void,Error>{if(publicLimits_==v){return {};}auto applied=pub.applyExternalRateLimits(v);if(applied){publicLimits_=v;}return applied;};
    const auto applyPriv=[&](const core::SessionRateLimits& v)->std::expected<void,Error>{if(!priv||privateLimits_==v){return {};}auto applied=priv->applyExternalRateLimits(v);if(applied){privateLimits_=v;}return applied;};
    // Reduce each independently before granting capacity released by its sibling.
    auto applied=applyPub(reduction(publicLimits_,p));if(!applied){return applied;}
    if(priv){applied=applyPriv(reduction(privateLimits_,q));if(!applied){return applied;}}
    applied=applyPub(p);if(!applied){return applied;}return priv?applyPriv(q):std::expected<void,Error>{};
  }
public:
  void configure(core::QueuePolicy policy){policy_=std::move(policy);lastPositions_.clear();refreshPositions_=true;publicLimits_.reset();privateLimits_.reset();privateOwner_={};}
  void privateOwnerChanged(){privateLimits_.reset();privateOwner_={};lastPositions_.clear();refreshPositions_=true;}
  void invalidatePositions(){lastPositions_.clear();refreshPositions_=true;}
  const std::optional<Error>& positionFailure()const noexcept{return positionFailure_;}
  std::expected<void,Error> tick(engine::PersistentSession& pub,OwnedI2pService* priv,
      const storage::ProfileLease& lease,const engine::NetworkPolicy& network){
    auto a=pub.externalQueueSnapshot();if(!a){return std::unexpected(a.error());}
    engine::ExternalQueueSnapshot b;if(priv){auto v=priv->externalQueueSnapshot();if(!v){return std::unexpected(v.error());}b=std::move(*v);}
    if(refreshPositions_){
      auto source=storage::readLibraryView(lease);if(!source){return std::unexpected(source.error());}
      std::map<std::string,std::optional<std::uint32_t>> saved;
      for(const auto& row:source->items){if(!row.saved.hash){continue;}const auto hash=row.saved.hash->hex();
        const auto pos=row.saved.position;
        std::optional<std::uint32_t> value;if(pos&&*pos>0&&*pos<=UINT32_MAX){value=static_cast<std::uint32_t>(*pos);}
        if(!saved.emplace(hash,value).second){saved[hash].reset();}
      }
      std::vector<std::pair<Binding,std::uint32_t>> p,q;
      for(const bool privateRow:{false,true}){auto& snapshot=privateRow?b:*a;for(auto& row:snapshot.rows){
        const auto found=saved.find(row.binding.hash.hex());if(found==saved.end()||!found->second){return fail("Saved shared queue positions are ambiguous");}
        row.facts.position=*found->second;(privateRow?q:p).push_back({row.binding,*found->second});
      }}
      auto applied=pub.applyExternalQueuePositions(p);if(!applied){return applied;}
      if(priv){applied=priv->applyExternalQueuePositions(q);if(!applied){return applied;}}
      refreshPositions_=false;
    }
    std::vector<core::QueueTorrentFacts> facts;
    std::vector<std::pair<bool,Binding>> bindings;
    std::set<Key> current;
    std::vector<storage::LibraryPositionObservation> positions;
    std::vector<storage::LibraryGroupTransition> transitions;
    std::vector<std::tuple<bool,std::uint64_t,storage::LibraryGroupTransition>> transitionReceipts;
    std::vector<std::tuple<Key,std::uint64_t,bool>> positionStamp;
    bool checked=true;
    for(const bool privateRow:{false,true}){auto& source=privateRow?b:*a;
      for(auto& row:source.rows){
        const Key key{privateRow,row.binding.hash.hex(),row.binding.admission};
        if(!row.binding.admission||!current.insert(key).second){return fail("Duplicate shared queue admission");}
        auto [token,added]=tokens_.try_emplace(key,0);if(added){if(nextToken_==UINT64_MAX){return fail("Shared queue identity space exhausted");}token->second=++nextToken_;}
        row.facts.token=token->second;facts.push_back(row.facts);bindings.push_back({privateRow,row.binding});
        checked=checked&&row.checked;positions.push_back(row.position);
        positionStamp.push_back({key,row.transitionOrder,row.position.complete});
        if(row.transition){transitionReceipts.push_back({privateRow,row.transitionOrder,*row.transition});}
      }
    }
    // Host receives the Public snapshot, then the private snapshot. Preserve
    // each source owner's actual transition order within those receipts.
    std::ranges::stable_sort(transitionReceipts,{},[](const auto& row){return std::pair{std::get<0>(row),std::get<1>(row)};});
    for(const auto& row:transitionReceipts){transitions.push_back(std::get<2>(row));}
    std::erase_if(tokens_,[&](const auto& row){return !current.contains(row.first);});
    // Original normalization requires current checked classification for the
    // entire inventory. Missing/stopped/unsupported rows remain unavailable.
    if(checked&&!positions.empty()&&positionStamp!=lastPositions_){
      std::optional<storage::LibraryPositionNormalization> normalized;
      const auto write=[&]()->std::expected<void,Error>{auto v=storage::normalizeLibraryPositions(lease,positions,transitions,policy_.newSeedsMoveTop);if(!v){return std::unexpected(v.error());}normalized=std::move(*v);return {};};
      auto saved=priv?priv->publishPositions(write):write();
      if(saved){
        std::vector<std::pair<Binding,std::uint32_t>> p,q;
        for(const auto& row:normalized->positions){for(std::size_t i=0;i<bindings.size();++i){if(bindings[i].second.hash==row.hash){facts[i].position=row.position;(bindings[i].first?q:p).push_back({bindings[i].second,row.position});}}}
        auto applied=pub.applyExternalQueuePositions(p);if(!applied){return applied;}
        if(priv){applied=priv->applyExternalQueuePositions(q);if(!applied){return applied;}}
        positionFailure_.reset();lastPositions_=std::move(positionStamp);
      }else{positionFailure_=saved.error();lastPositions_=std::move(positionStamp);}
    }
    core::QueueContext context=a->context;
    if(priv){
      context.nowMillis=std::max(context.nowMillis,b.context.nowMillis);
      context.ownerStartedMillis=std::min(context.ownerStartedMillis,b.context.ownerStartedMillis);
      context.downloadBytesPerSecond=sum(context.downloadBytesPerSecond,b.context.downloadBytesPerSecond);
      context.uploadBytesPerSecond=sum(context.uploadBytesPerSecond,b.context.uploadBytesPerSecond);
      context.smoothedUploadBytesPerSecond=sum(context.smoothedUploadBytesPerSecond,b.context.smoothedUploadBytesPerSecond);
      context.downloadProbeUnavailable=context.downloadProbeUnavailable||b.context.downloadProbeUnavailable;
    }
    context.seedingOnly=!facts.empty()&&std::ranges::all_of(facts,[](const auto& row){return row.complete;});
    context.downloadLimitBytesPerSecond=network.downloadBytesPerSecond;
    context.uploadLimitBytesPerSecond=network.seedingOnlyUpload&&context.seedingOnly?network.seedingUploadBytesPerSecond:network.uploadBytesPerSecond;
    if(!b.rows.empty()&&(policy_.downloadOrder==core::QueueDownloadOrder::Speed||policy_.downloadOrder==core::QueueDownloadOrder::Eta)){
      // Public's ten-slot receive average cannot authorise a probe over two
      // owners. Keep the original unavailable/position fallback explicit.
      for(auto& row:facts){row.downloadProbe.reset();}context.downloadProbeUnavailable=true;
    }
    auto plan=core::evaluateQueue(policy_,facts,context);
    if(!plan){return fail("The shared original queue rejected its current observations");}
    std::vector<Decision> p,q;bool publicDemand=false,privateDemand=false;
    for(std::size_t i=0;i<plan->decisions.size();++i){
      auto d=plan->decisions[i];d.token=bindings[i].second.admission;
      (bindings[i].first?q:p).push_back({bindings[i].second,d});
      const auto state=facts[i].state;
      const bool retainedDemand=d.action==core::QueueAction::Preserve&&!facts[i].manuallyPaused
          &&(state==core::QueueTorrentState::Downloading||state==core::QueueTorrentState::Seeding);
      if(d.action==core::QueueAction::Run||retainedDemand){(bindings[i].first?privateDemand:publicDemand)=true;}
    }
    auto caps=rates(pub,priv,network,context.seedingOnly,publicDemand,privateDemand);if(!caps){return caps;}
    const auto pausedPublic=pub.applyExternalQueue(a->sequence,p,plan->unavailableInputs,core::QueueAction::Queue);
    if(!pausedPublic){return std::unexpected(pausedPublic.error());}
    bool pausedPrivate=true;if(priv){const auto v=priv->applyExternalQueue(b.sequence,q,plan->unavailableInputs,core::QueueAction::Queue);if(!v){return std::unexpected(v.error());}pausedPrivate=*v;}
    if(!*pausedPublic||!pausedPrivate){return {};}
    auto run=pub.applyExternalQueue(a->sequence,p,plan->unavailableInputs,core::QueueAction::Run);if(!run){return std::unexpected(run.error());}
    if(priv){run=priv->applyExternalQueue(b.sequence,q,plan->unavailableInputs,core::QueueAction::Run);if(!run){return std::unexpected(run.error());}}
    return {};
  }
};
}
