// SPDX-License-Identifier: GPL-2.0-or-later
#include "peer_set_tag_turn.hpp"
#include <limits>
namespace biglybtc::app {
std::expected<ConstraintWorkerUpdate,storage::Error> runConstraintPeerTurn(
 TagConstraintWorker& worker,storage::ManualTagStore& store,const storage::ManualTagSnapshot& tags,
 ConstraintWorkerFrame& frame,engine::PersistentSession* engine,std::stop_token stop){
 auto first=worker.step(store,tags,frame,stop);if(!first||!first->peerRequest||!engine||stop.stop_requested()){return first;}
 if(frame.sequence==std::numeric_limits<std::uint64_t>::max()){
  return std::unexpected(storage::Error{storage::ErrorCode::Unsupported,"Peer-set worker sequences are exhausted"});}
 auto request=std::move(*first->peerRequest);auto hash=constraintFactHash(request.stamp.binding.target.hash);
 ConstraintPeerObservation observation{std::move(request),{},std::chrono::steady_clock::now()};
 if(hash){auto value=engine->inspectPeerSetEndpoints(*hash,observation.request.stamp.binding.providerAdmission);if(value){observation.value=std::move(*value);}}
 std::erase_if(frame.peerObservations,[&](const auto& old){return old.request.stamp.binding.target.hash==observation.request.stamp.binding.target.hash;});
 if(frame.peerObservations.size()==32){frame.peerObservations.erase(frame.peerObservations.begin());}frame.peerObservations.push_back(std::move(observation));
 ++frame.sequence;frame.refreshSettings=false;
 // SDK is observational; step rechecks current inventory,state,definitions,
 // provider stamps,cancellation and actual Store CAS before a saved effect.
 return worker.step(store,tags,frame,stop);
}
}
