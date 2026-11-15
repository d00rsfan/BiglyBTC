// SPDX-License-Identifier: GPL-2.0-or-later
#include "../services/azureus_db_runtime/cache_republication.hpp"
#include <biglybtc/bencode.hpp>
#include <algorithm>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>
namespace core=biglybtc::core;namespace db=biglybtc::services::azureus_db_runtime;
namespace {
void require(bool valid,const std::string& reason){if(!valid){throw std::runtime_error(reason);}}
const core::Node& field(const core::Node& row,std::string_view name){const auto* n=row.find(name);require(n!=nullptr,"Missing original fixture field");return *n;}
std::vector<std::int64_t> indices(const core::Node& list){std::vector<std::int64_t> out;for(const auto& n:list.children){out.push_back(n.integer);}return out;}
}
int main(int argc,char** argv){try{
 require(argc==2,"Original cache fixture path required");std::ifstream input(argv[1],std::ios::binary);require(bool(input),"Original cache fixture unavailable");const std::string bytes((std::istreambuf_iterator<char>(input)),{});auto doc=core::Document::parse(bytes);require(bool(doc),"Original cache fixture invalid");const auto& cases=field(doc->root(),"cases");require(cases.children.size()==19,"Expected19 actual original scenarios");
 for(const auto& row:cases.children){
  const std::string name(field(row,"name").bytes);std::vector<db::CachedValueAge> values;
  for(const auto& value:field(row,"input").children){values.push_back({field(value,"local").integer!=0,static_cast<std::int8_t>(field(value,"rep").integer),field(value,"stored").integer});}
  auto selected=db::selectCachedValues(static_cast<std::int8_t>(field(row,"div").integer),values,field(row,"now").integer,field(row,"period").integer);require(bool(selected),name+": selection rejected");auto due=indices(field(row,"due"));if(name=="token-failure"){require(field(row,"tokens").integer==1&&field(row,"dead").integer==1,name+": original token failure changed");due={0};}
  require(std::ranges::equal(selected->dueValues(),due),name+": original due values differ");require(std::ranges::equal(selected->clockRollbacks(),indices(field(row,"reset"))),name+": original clock rollback differs");const bool survey=name=="explicit-only"||name=="zero-factor"||name=="explicit-future"||name=="survey-only-drop";require(selected->surveyOnly==survey,name+": survey disposition differs");require(selected->skipped==(field(row,"div").integer!=1),name+": diversified mapping not skipped");
  for(const auto& relay:field(row,"relayed").children){const auto index=field(relay,"version").integer-10;require(index>=0&&std::size_t(index)<values.size(),name+": original relay version");require(field(relay,"flags").integer==0x21&&field(relay,"life").integer==72&&field(relay,"rep").integer==values[std::size_t(index)].replication,name+": original relay fields");const auto sender=field(relay,"sender").bytes;require(sender.size()==20&&sender[0]==1&&std::ranges::all_of(sender.substr(1),[](char c){return c==0;}),name+": relay sender is not actual local contact");}
  if(name=="last-forward-before-drop"){require(field(row,"kept").integer==0&&field(row,"operations").integer==1,"Original final forward/drop changed");}
  if(name=="survey-only-drop"){require(field(row,"kept").integer==0&&field(row,"operations").integer==0,"Original survey-only drop changed");}
 }
 const std::array equality{db::CachedValueAge{false,-1,1000}};auto exact=db::selectCachedValues(1,equality,2000,1000);require(exact&&exact->dueCount==0,"Age equality must not republish");auto after=db::selectCachedValues(1,equality,2001,1000);require(after&&after->dueCount==1,"Age strict greater must republish");auto future=db::selectCachedValues(1,equality,999,1000);require(future&&future->rollbackCount==1&&future->dueCount==0,"Future store time must reset without forwarding");std::array<db::CachedValueAge,257> tooMany{};require(!db::selectCachedValues(1,tooMany,1000,1000),"Value capacity must remain bounded");require(!db::selectCachedValues(0,equality,2000,1000)&&!db::selectCachedValues(1,equality,2000,-1),"Invalid policy must not infer permission");
 std::cout<<"CACHE_SELECTION original_cases="<<cases.children.size()<<" boundary_groups=5 PASS\n";return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
