// SPDX-License-Identifier: GPL-2.0-or-later
#include "../services/azureus_db_runtime/survey_topology.hpp"
#include "../services/azureus_db_runtime/survey_repairs.hpp"
#include <biglybtc/bencode.hpp>
#include <algorithm>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <set>
#include <stdexcept>
namespace core=biglybtc::core;namespace graph=biglybtc::services::azureus_db_runtime::survey_graph;
namespace {
void require(bool ok,const std::string& why){if(!ok){throw std::runtime_error(why);}}
const core::Node& field(const core::Node& row,std::string_view key){const auto* value=row.find(key);require(value!=nullptr,"Original graph field missing");return *value;}
graph::NodeId id(std::string_view bytes){require(bytes.size()==20,"Original graph ID width");graph::NodeId out;std::copy(bytes.begin(),bytes.end(),out.begin());return out;}
using Groups=std::map<std::string,std::multiset<std::string>>;
Groups groups(const core::Node& encoded){Groups out;for(const auto& group:encoded.children){const std::string prefix(field(group,"prefix").bytes);for(const auto& suffix:field(group,"suffixes").children){out[prefix].insert(std::string(suffix.bytes));}}return out;}
}
int main(int argc,char** argv){try{
 require(argc==2,"Original graph fixture required");std::ifstream in(argv[1],std::ios::binary);const std::string bytes((std::istreambuf_iterator<char>(in)),{});auto document=core::Document::parse(bytes);require(bool(document),"Original graph fixture parse");const auto& cases=field(document->root(),"cases");require(cases.children.size()==15,"Original graph case count");
 for(const auto& row:cases.children){
  const std::string name(field(row,"name").bytes);const auto local=id(field(row,"local").bytes);std::vector<graph::Node> nodes;for(const auto& n:field(row,"contacts").children){graph::Node node;node.id=id(field(n,"id").bytes);auto ip=field(n,"ip").bytes;node.addressLength=std::uint8_t(ip.size());node.protocol=std::int8_t(field(n,"protocol").integer);std::copy(ip.begin(),ip.end(),node.address.begin());nodes.push_back(node);}if(field(row,"include_local").integer){graph::Node node;node.id=local;nodes.push_back(node);}std::vector<std::string> keys;for(const auto& key:field(row,"keys").children){keys.emplace_back(key.bytes);}auto topology=graph::topology(local,std::size_t(field(row,"k").integer),nodes,keys);require(bool(topology),name+": graph refused");
  std::map<graph::NodeId,Groups> expected;for(const auto& query:field(row,"queries").children){require(field(query,"header").integer==6,name+": original header changed");expected.emplace(id(field(query,"contact").bytes),groups(field(query,"groups")));}
  std::map<graph::NodeId,Groups> actual;for(const auto& query:topology->queries){Groups g;std::size_t offset=0;for(const auto& group:query.request.groups){for(const auto& suffix:group.suffixes){g[group.prefix].insert(suffix);require(offset<query.keys.size()&&group.prefix+suffix==keys[query.keys[offset++]].substr(0,6),name+": query/reply row association");}}require(offset==query.keys.size(),name+": row association count");if(query.wireEligible){actual.emplace(nodes[query.node].id,std::move(g));}}require(actual==expected,name+": actual original query graph differs");
  std::vector<graph::Knowledge> knowledge(nodes.size());for(std::size_t i=0;i<nodes.size();++i){knowledge[i].id=nodes[i].id;}
  const auto response=field(row,"response").bytes;
  for(const auto& query:topology->queries){auto& state=knowledge[query.node];state.hasState=true;state.responseCounted=query.wireEligible&&response!="failed"&&response!="short";state.consecutiveFailures=!query.wireEligible||response=="failed"?1:0;if(query.wireEligible&&(response=="present"||(response=="one-present"&&nodes[query.node].id[0]==2))){state.knownKeys=query.keys;}}
  const auto rep=field(row,"replication").integer;std::vector<std::int8_t> factors(keys.size(),std::int8_t(rep==255||rep==-1?-1:rep&15));auto repair=graph::repairs(*topology,local,knowledge,factors);require(bool(repair),name+": repair plan refused");std::map<graph::NodeId,std::set<std::string>> expectedStores,actualStores;
  for(const auto& store:field(row,"stores").children){auto& set=expectedStores[id(field(store,"contact").bytes)];for(const auto& key:field(store,"keys").children){set.insert(std::string(key.bytes));}}
  for(std::size_t key=0;key<repair->targetsByKey.size();++key){for(const auto node:repair->targetsByKey[key]){actualStores[nodes[node].id].insert(keys[key]);}}require(actualStores==expectedStores,name+": actual original repair targets differ");
  auto changedLocal=local;changedLocal[19]^=1;require(!graph::repairs(*topology,changedLocal,knowledge,factors),name+": changed router identity accepted");

 }
 const graph::NodeId local{};const std::array nodes{graph::Node{}};const std::vector<std::string> keys{std::string(20,'K')};require(!graph::topology(local,0,nodes,keys),"Zero K refused");require(!graph::topology(local,65,nodes,keys),"Oversized K refused");const std::vector<graph::Node> many(101);require(!graph::topology(local,20,many,keys),"Survey contact bound");const std::vector<graph::Node> duplicate(2);require(!graph::topology(local,20,duplicate,keys),"Duplicate node identity");const std::vector<std::string> invalid{"short"},duplicates(2,std::string(20,'K'));require(!graph::topology(local,20,nodes,invalid)&&!graph::topology(local,20,nodes,duplicates),"Invalid/duplicate keys refused");
 std::cout<<"SURVEY_TOPOLOGY_REPAIRS original_cases="<<cases.children.size()<<" boundary_groups=5 PASS\n";return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
