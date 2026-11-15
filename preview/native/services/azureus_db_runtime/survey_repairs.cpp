// SPDX-License-Identifier: GPL-2.0-or-later
// Original DHTDBImpl.queriesComplete: Copyright (C) Azureus Software, Inc.
#include "survey_repairs.hpp"
#include <algorithm>
namespace biglybtc::services::azureus_db_runtime::survey_graph {
std::expected<Repairs,Error> repairs(const Topology& topology,const NodeId& currentLocal,std::span<const Knowledge> knowledge,std::span<const std::int8_t> factors){
 if(topology.local!=currentLocal||knowledge.size()!=topology.nodes.size()||factors.size()!=topology.keys.size()||topology.peersByKey.size()!=factors.size()||topology.k==0||topology.k>64){return std::unexpected(Error::Input);}
 if(knowledge.size()>100||factors.size()>8192){return std::unexpected(Error::Capacity);}
 const auto& nodes=topology.nodes;const auto count=factors.size();std::vector<std::vector<bool>> have(nodes.size(),std::vector<bool>(count));std::vector<std::size_t> copies(count,1);
 for(std::size_t node=0;node<nodes.size();++node){
  const auto& state=knowledge[node];if(state.id!=nodes[node].id||state.knownKeys.size()>count){return std::unexpected(Error::Input);}
  for(const auto key:state.knownKeys){if(key>=count||have[node][key]){return std::unexpected(Error::Input);}have[node][key]=true;if(state.hasState&&state.responseCounted&&nodes[node].id!=currentLocal){++copies[key];}}
 }
 Repairs result;result.targetsByKey.resize(count);
 for(std::size_t key=0;key<count;++key){
  if(factors[key]< -1||factors[key]>15||topology.keys[key].size()!=20){return std::unexpected(Error::Input);}const auto maximum=std::min<int>(factors[key],int(topology.k));if(maximum<=0||copies[key]>=std::size_t(maximum)){continue;}
  const auto& peers=topology.peersByKey[key];std::vector<std::size_t> targets;std::vector<bool> bad(nodes.size());
  for(const auto node:peers){if(node>=nodes.size()||nodes[node].id==currentLocal){return std::unexpected(Error::Input);}if(nodes[node].protocol>=26&&knowledge[node].hasState&&!have[node][key]){targets.push_back(node);}}
  for(const auto a:peers){for(const auto b:peers){if(a!=b&&nodes[a].protocol>=26&&nodes[b].protocol>=26&&nodes[a].addressLength==4&&nodes[b].addressLength==4&&nodes[a].address[0]==nodes[b].address[0]&&nodes[a].address[1]==nodes[b].address[1]){bad[a]=bad[b]=true;}}}
  std::sort(targets.begin(),targets.end(),[&](const auto a,const auto b){const bool aFailed=knowledge[a].consecutiveFailures>=2,bFailed=knowledge[b].consecutiveFailures>=2;if(aFailed!=bFailed){return !aFailed;}for(std::size_t i=0;i<20;++i){const auto x=nodes[a].id[i]^static_cast<unsigned char>(topology.keys[key][i]);const auto y=nodes[b].id[i]^static_cast<unsigned char>(topology.keys[key][i]);if(x!=y){return x<y;}}return false;});
  const auto available=std::min(std::size_t(maximum)-copies[key],targets.size());
  // Original subnet avoidance consumes that candidate slot; it does not retry
  // a farther peer and thereby bias placement towards an observed subnet.
  for(std::size_t i=0;i<available;++i){if(bad[targets[i]]){result.assumed.push_back({key,targets[i]});}else{result.targetsByKey[key].push_back(targets[i]);}}
 }
 return result;
}
}
