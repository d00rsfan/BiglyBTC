// SPDX-License-Identifier: GPL-2.0-or-later
// Original DHTDBImpl.processSurvey: Copyright (C) Azureus Software, Inc.
#include "survey_topology.hpp"
#include <algorithm>
#include <map>
#include <numeric>
#include <set>
namespace biglybtc::services::azureus_db_runtime::survey_graph {
namespace {
bool closer(const NodeId& a,const NodeId& b,std::string_view key){
 for(std::size_t i=0;i<20;++i){const auto x=a[i]^static_cast<unsigned char>(key[i]);const auto y=b[i]^static_cast<unsigned char>(key[i]);if(x!=y){return x<y;}}return false;
}
}
std::expected<Topology,Error> topology(const NodeId& local,std::size_t k,std::span<const Node> nodes,std::span<const std::string> keys){
 if(k==0||k>64){return std::unexpected(Error::Input);}if(nodes.size()>100||keys.size()>8192){return std::unexpected(Error::Capacity);}
 std::set<NodeId> uniqueNodes;for(const auto& node:nodes){if(!uniqueNodes.insert(node.id).second){return std::unexpected(Error::Input);}}
 std::set<std::string_view> uniqueKeys;for(const auto& key:keys){if(key.size()!=20||!uniqueKeys.insert(key).second){return std::unexpected(Error::Input);}}
 Topology result;result.local=local;result.k=k;result.nodes.assign(nodes.begin(),nodes.end());result.keys.assign(keys.begin(),keys.end());result.peersByKey.resize(keys.size());std::vector<std::vector<std::size_t>> byNode(nodes.size());std::vector<std::size_t> sorted(nodes.size());std::iota(sorted.begin(),sorted.end(),0);
 for(std::size_t key=0;key<keys.size();++key){
  std::sort(sorted.begin(),sorted.end(),[&](auto a,auto b){return closer(nodes[a].id,nodes[b].id,keys[key]);});const auto count=std::min(k,sorted.size());bool hasLocal=false;
  for(std::size_t i=0;i<count;++i){if(nodes[sorted[i]].id==local){hasLocal=true;break;}}if(!hasLocal){continue;}
  for(std::size_t i=0;i<count;++i){const auto node=sorted[i];if(nodes[node].id!=local){result.peersByKey[key].push_back(node);byNode[node].push_back(key);}}
 }
 for(std::size_t node=0;node<nodes.size();++node){
  if(byNode[node].empty()){continue;}std::map<std::string,std::vector<std::size_t>> prefixes;prefixes.emplace("",std::move(byNode[node]));
  for(std::size_t depth=0;depth<3;++depth){
   std::vector<std::string> previous;for(const auto& [prefix,indices]:prefixes){if(prefix.size()==depth&&indices.size()>=2){previous.push_back(prefix);}}
   for(const auto& prefix:previous){
    auto at=prefixes.find(prefix);std::map<std::string,std::vector<std::size_t>> extended;
    for(const auto key:at->second){extended[keys[key].substr(0,depth+1)].push_back(key);}std::vector<std::size_t> retained;
    for(auto& [next,indices]:extended){const auto outer=indices.size()*(6-depth);const auto inner=depth+4+indices.size()*(5-depth);if(inner<outer){prefixes.emplace(next,std::move(indices));}else{retained.insert(retained.end(),indices.begin(),indices.end());}}
    if(retained.empty()){prefixes.erase(at);}else{at->second=std::move(retained);}
   }
  }
  Query query;query.node=node;query.wireEligible=nodes[node].protocol>=26;for(const auto& [prefix,indices]:prefixes){azureus_query_storage::KeyGroup group;group.prefix=prefix;for(const auto key:indices){group.suffixes.push_back(keys[key].substr(prefix.size(),6-prefix.size()));query.keys.push_back(key);}query.request.groups.push_back(std::move(group));}result.queries.push_back(std::move(query));
 }
 return result;
}
}
