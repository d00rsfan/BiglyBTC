// SPDX-License-Identifier: GPL-2.0-or-later
#include "peer_set_tag_turn.hpp"
#include <limits>
namespace biglybtc::app {
std::expected<ConstraintWorkerUpdate,storage::Error> runConstraintPeerTurn(
 TagConstraintWorker& worker,storage::ManualTagStore& store,const storage::ManualTagSnapshot& tags,
 ConstraintWorkerFrame& frame,const storage::ProfileLease& lease,
 engine::PersistentSession* engine,std::stop_token stop,OwnedI2pService* i2p){
 auto result=worker.step(store,tags,frame,stop);
 bool selected=false,complete=false;
 while(result&&result->peerRequest&&!stop.stop_requested()){
  const auto scope=result->peerRequest->scope;
  const bool isPrivate=scope==ConstraintPeerRequest::Scope::SelectedI2p;
  if((isPrivate&&!i2p)||(!isPrivate&&!engine)){break;}
  bool& queried=scope==ConstraintPeerRequest::Scope::Complete?complete:selected;
  if(queried){break;}
  queried=true;
  if(frame.sequence==std::numeric_limits<std::uint64_t>::max()){
   return std::unexpected(storage::Error{storage::ErrorCode::Unsupported,"Peer-set worker sequences are exhausted"});}
  auto request=std::move(*result->peerRequest);
  const auto sampled=std::chrono::steady_clock::now();
  if(scope==ConstraintPeerRequest::Scope::Complete){
   auto value=engine->inspectPeerSetCensus(lease);
   frame.peerCensus=ConstraintPeerCensusObservation{std::move(request),{},sampled};
   if(value){frame.peerCensus->value=std::move(*value);}
  }else{
   const auto hash=constraintFactHash(request.stamp.binding.target.hash);
   ConstraintPeerObservation observation{std::move(request),{},sampled};
   if(hash){
    const auto& binding=observation.request.stamp.binding;
    if(isPrivate){
     observation.privateI2pCount=i2p->peerSetCount(*hash,binding.providerAdmission,binding.providerGeneration);
    }else{
     auto value=engine->inspectPeerSetEndpoints(*hash,binding.providerAdmission);
     if(value){observation.value=std::move(*value);}
    }
   }
   std::erase_if(frame.peerObservations,[&](const auto& old){
    return old.request.stamp.binding.target.hash==observation.request.stamp.binding.target.hash;});
   if(frame.peerObservations.size()==32){frame.peerObservations.erase(frame.peerObservations.begin());}
   frame.peerObservations.push_back(std::move(observation));
  }
  ++frame.sequence;frame.refreshSettings=false;
  // The worker rechecks inventory, state, definitions, provider stamps and
  // the actual Store CAS after the owning SDK observation.
  result=worker.step(store,tags,frame,stop);
 }
 return result;
}
}
