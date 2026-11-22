// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "tag_constraint_worker.hpp"
#include "profile.hpp"
namespace biglybtc::app::detail {
// The existing Host worker invokes the coordinator only for an actual CAS.
// It joins private readers without an FS gate. Reacquire and repeat every
// producer preimage check before the one durable write;drop this local gate
// before the coordinator acknowledges any SDK/source effects.
inline const OwnedI2pService::TorrentObservation* privateRateSubject(
    const ConstraintWorkerFrame& frame,const core::TorrentHash& hash){
 if(!frame.privateRateWrite){return nullptr;}const OwnedI2pService::TorrentObservation* found=nullptr;
 for(const auto& row:frame.i2p){if(row.hash!=hash){continue;}
  if(found||!row.value.current||!row.value.admission||!row.value.generation){return nullptr;}found=&row;
 }return found;
}
inline bool privateRateAllowed(const ConstraintWorkerFrame& frame,
    const storage::ManualTagSnapshot& snapshot,const core::TorrentHash& hash,std::int32_t tag){
 const auto* type=snapshot.tags()&&snapshot.tags()->source?snapshot.tags()->source->find("3"):nullptr;
 const auto* record=type?type->find(std::to_string(tag)):nullptr;
 return privateRateSubject(frame,hash)&&record&&core::rateOnlySavedTagMembershipEffects(*snapshot.tags(),*record);
}
struct PrivateRateRequest {
 core::TorrentHash hash;std::uint64_t admission,generation;
 std::vector<storage::ManualTagMembershipEdit> edits;
};
inline std::optional<PrivateRateRequest> privateRateRequest(const ConstraintWorkerFrame& frame,
    const storage::ManualTagSnapshot& snapshot,std::span<const storage::ManualTagMembershipEdit> edits){
 if(edits.empty()||edits.size()>32){return {};}
 const auto hash=std::visit([](const auto& item){return item.hash;},edits.front());
 const auto* subject=privateRateSubject(frame,hash);if(!subject){return {};}
 for(const auto& edit:edits){if(!std::visit([&](const auto& item){return item.hash==hash&&privateRateAllowed(frame,snapshot,hash,item.id);},edit)){return {};}}
 return PrivateRateRequest{hash,subject->value.admission,subject->value.generation,{edits.begin(),edits.end()}};
}
template<class Gate,class Validate,class Write>
std::expected<storage::ManualTagCommit,storage::Error> publishCurrentTagCommit(
    const ConstraintWorkerFrame& frame,const storage::ProfileLease& lease,
    Gate& held,Validate validate,Write write,std::optional<PrivateRateRequest> privateRequest={}){
 if(!frame.publishTagChange&&!privateRequest){return write();}
 const auto revision=lease.sourceRevision();
 auto original=storage::ConfigFile::load(lease,"biglybt.config");
 if(!original&&original.error().code!=storage::ErrorCode::Missing){return std::unexpected(original.error());}
 const std::optional<std::string> config=original?std::optional{original->document().serialize()}:std::nullopt;
 held=std::unexpected(storage::Error{storage::ErrorCode::Cancelled,"Source gate released for tag publication"});
 const auto revalidate=[&](const storage::ProfileSourceAccess& gate)->std::expected<void,storage::Error>{
  if(gate.revision()!=revision){return std::unexpected(storage::Error{storage::ErrorCode::Conflict,"Tag publication source turn changed while quiescing private readers"});}
  auto current=storage::ConfigFile::load(lease,"biglybt.config");
  if(!current&&current.error().code!=storage::ErrorCode::Missing){return std::unexpected(current.error());}
  if(bool(current)!=bool(config)||(current&&current->document().serialize()!=*config)){
   return std::unexpected(storage::Error{storage::ErrorCode::Conflict,"Tag publication configuration changed while quiescing private readers"});
  }
  return validate(gate);
 };
 if(privateRequest){
  if(!frame.privateRateWrite){return std::unexpected(storage::Error{storage::ErrorCode::Unsupported,"Actual private rate membership writer required"});}
  const auto& request=*privateRequest;return frame.privateRateWrite(request.hash,request.admission,request.generation,request.edits,revalidate);
 }
 return frame.publishTagChange([&]()->std::expected<storage::ManualTagCommit,storage::Error>{
  auto gate=lease.sourceAccess();if(!gate){return std::unexpected(gate.error());}
  auto checked=revalidate(*gate);if(!checked){return std::unexpected(checked.error());}return write();
 });
}
}
