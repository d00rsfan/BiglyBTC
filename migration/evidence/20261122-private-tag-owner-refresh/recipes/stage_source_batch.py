from pathlib import Path
import hashlib,json
D=Path(__file__).resolve().parent;N=D.parents[1].parent/'BiglyBTC/preview/native';O=D/'drafts';B=D/'baseline';pins={}
for name in ['services/i2p_provider/profile_source.hpp','services/i2p_provider/profile_source.cpp']:
 target=B/name;target.parent.mkdir(parents=True,exist_ok=True)
 if not target.exists():target.write_bytes((N/name).read_bytes())
 pins[name]=hashlib.sha256(target.read_bytes()).hexdigest()
 def write(s):
  p=O/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_text(s)
 s=target.read_text()
 if name.endswith('.hpp'):
  s=s.replace('class OwnedProfile;','class OwnedProfile;\nstruct TagSourceRefresh;',1)
  s=s.replace('class PrivateTagRateSource;','class PrivateTagRateSource;class ProfileTagRates;',1)
  needle='  std::expected<engine::detail::ManagedStorage,storage::Error> prepareStorageDirect();';assert s.count(needle)==1
  s=s.replace(needle,'''  bool matchesTagOwner(const engine::ProfileTagRates&)const noexcept;
  // Trusted owner only,after both real source workers have joined. FS-only;
  // all unrelated snapshots remain exact and every retained child participates.
  std::expected<void,storage::Error> acceptTagSources(std::span<const TagSourceRefresh>);
'''+needle);write(s)
 else:
  s=s.replace('#include "profile_source.hpp"','#include "profile_source.hpp"\n#include "private_tag_source_refresh.hpp"\n#include "profile_tag_rates.hpp"',1)
  needle='  bool namedCurrent()const{\n    if(rateSource&&!sameRates(*rateSource,rateStamps(*lease))){return false;}';assert s.count(needle)==1
  s=s.replace(needle,'  bool namedCurrent(bool includeRates=true)const{\n    if(includeRates&&rateSource&&!sameRates(*rateSource,rateStamps(*lease))){return false;}')
  needle='std::expected<void,storage::Error> ProfileSource::validate()const{';assert s.count(needle)==1
  s=s.replace(needle,'''bool ProfileSource::matchesTagOwner(const engine::ProfileTagRates& rates)const noexcept{
 try{
  if(!impl_->session||impl_->mixed||impl_->revoked||!impl_->tagMode
      ||!rates.matchesProfile(*impl_->lease,*impl_->tagMode)){return false;}
  bool any=false;
  for(const auto& weak:impl_->children){if(auto child=weak.lock();child&&!child->impl_->revoked&&child->impl_->stamp){
    const auto prepared=child->impl_->privateRates.load();
    if(!prepared||!rates.ownsPrivateSource(prepared->source)){return false;}any=true;
  }}return any;
 }catch(...){return false;}
}
std::expected<void,storage::Error> ProfileSource::acceptTagSources(std::span<const TagSourceRefresh> updates){
  try{
    auto access=take(impl_->lease->sourceAccess());
    require(impl_->session&&!impl_->mixed&&!impl_->publishing&&!impl_->revoked&&impl_->stamp
        &&updates.size()<=32,ErrorCode::Conflict,"Current dedicated private source refresh anchor required");
    std::vector<std::shared_ptr<ProfileSource>> children;
    for(const auto& weak:impl_->children){if(auto child=weak.lock();child&&!child->impl_->revoked&&child->impl_->stamp){children.push_back(std::move(child));}}
    require(!children.empty()&&children.size()==updates.size(),ErrorCode::Conflict,"Private tag refresh requires every retained prepared child");
    const auto unrelatedCurrent=[&]{
      if(impl_->revoked||!impl_->namedCurrent(false)
          ||capture(*impl_->lease,nullptr,impl_->directory,true,false)!=*impl_->stamp){return false;}
      for(const auto& child:children){const auto& member=*child->impl_;
        if(member.revoked||member.anchor.get()!=this||member.pending||member.mixed||member.session
            ||!member.namedCurrent(false)||!member.managedCurrent()||!member.snapshot()
            ||member.memberSnapshot(true)!=*member.snapshot()){return false;}
      }return true;
    };
    require(unrelatedCurrent(),ErrorCode::Conflict,"Private tag refresh changed unrelated profile/member metadata");
    const auto before=rateStamps(*impl_->lease);
    auto store=take(storage::ManualTagStore::open(*impl_->lease));auto snapshot=take(store->snapshot());
    require(snapshot.writable(),ErrorCode::Conflict,"Private tag refresh requires its intact current primary source");
    struct Prepared{std::shared_ptr<ProfileSource> child;std::shared_ptr<const PrivateRatePreparation> value;};
    std::vector<Prepared> prepared;prepared.reserve(children.size());
    for(const auto& child:children){
      const TagSourceRefresh* update=nullptr;
      for(const auto& candidate:updates){if(candidate.child==child){require(!update,ErrorCode::Conflict,"Duplicate private tag refresh child");update=&candidate;}}
      const auto old=child->impl_->privateRates.load();
      require(update&&old&&child->impl_->tagMode&&child->impl_->tagMode->enabled()
          &&child->impl_->tagMode->matches(*impl_->lease)
          &&update->source.matches(*impl_->lease,child->impl_->hash,snapshot,old->expected,old->session),
          ErrorCode::Conflict,"Private tag refresh requires its original actual SDK/full target and fresh sealed source");
      auto value=std::make_shared<PrivateRatePreparation>(*old);value->source=update->source;
      prepared.push_back({child,std::move(value)});
    }
    const auto after=rateStamps(*impl_->lease);
    require(sameRates(before,after)&&unrelatedCurrent(),ErrorCode::Conflict,"Private source changed while preparing the complete tag refresh batch");
    for(const auto& value:prepared){require(value.value->source.current(),ErrorCode::Conflict,"Private tag SDK cohort expired before publication");}
    const auto revision=impl_->lease->sourceRevision();
    for(const auto& value:prepared){auto& member=*value.child->impl_;
      member.privateRates.store(value.value);member.rateSource=after;member.dirty=false;
      if(member.authority){member.authority->revision_=revision;}
    }
    impl_->dirty=false;if(impl_->authority){impl_->authority->revision_=revision;}
    require(!impl_->revoked&&unrelatedCurrent(),ErrorCode::Conflict,"Private source withdrew during tag refresh publication");
    for(const auto& value:prepared){require(value.value->source.current(),ErrorCode::Conflict,"Private tag SDK cohort expired during publication");}
    return {};
  }catch(const Rejected& error){revoke();return std::unexpected(error.error);}
  catch(...){revoke();return std::unexpected(Error{ErrorCode::Io,"Cannot publish private tag source refresh batch"});}
}

'''+needle);write(s)
p=O/'services/i2p_provider/private_tag_source_refresh.hpp';p.write_text('''// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "private_tag_rate_source.hpp"
#include <memory>
namespace biglybtc::services::i2p::provider {
class ProfileSource;
// Metadata for the private friend-only batch,not a source/storage/SDK grant.
struct TagSourceRefresh {
 std::shared_ptr<ProfileSource> child;
 engine::PrivateTagRateSource source;
};
}
''')
(D/'source-batch-baseline-sha256.json').write_text(json.dumps(pins,indent=2)+'\n');print('Staged private friend-only complete source/stamp refresh;no generic CAS or Native change')
