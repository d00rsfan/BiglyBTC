// SPDX-License-Identifier: GPL-2.0-or-later
#include <biglybtc/tag_constraints.hpp>
#include <biglybtc/bencode.hpp>
#include <biglybtc/tag_named_metrics.hpp>
#include <fstream>
#include <iostream>
#include <stdexcept>
namespace b=biglybtc::core;namespace c=b::tags::constraints;namespace r=b::tags::runtime;
namespace {
void need(bool ok,const char* why){if(!ok){throw std::runtime_error(why);}}
c::Facts facts(bool witness,bool alias,bool peerType=true){
 r::Tag peer;peer.id=1;peer.taggableTypes=4;peer.name=u"Generated-1";peer.group=u"Generated group";peer.membership=r::Membership::Absent;
 r::Type manual;manual.id=3;manual.complete=true;if(alias){auto item=peer;item.id=2;item.taggableTypes=2;item.membership=r::Membership::Present;manual.tags.push_back(item);}
 std::vector<r::Type> types;types.push_back(std::move(manual));if(peerType){types.push_back({4,{peer},true});}c::Facts out;out.tagRegistry=*r::Registry::create(std::move(types),true);if(witness){out.peerSetTagNames.push_back(u"Generated-1");}return out;
}
void actualFunctionsRetainDistinctSources(){
 std::ifstream file(PEER_SET_CACHE_FIXTURE,std::ios::binary);auto source=b::Document::parse(std::string{std::istreambuf_iterator<char>(file),{}});need(bool(source),"Actual original peer cache fixture");unsigned compared=0,unavailable=0;
 for(const auto& row:source->find("rows")->children){
  const auto fn=row.find("function")->bytes;const std::string name=fn=="hasTagGroup"?"Generated group":"Generated-1";const auto text=std::string(fn)+"(\""+name+"\")";const std::u16string expression(text.begin(),text.end());auto compiled=c::compile(expression);need(bool(compiled),"Generated actual peer expression");auto input=facts(row.find("peer_cache")->integer!=0,row.find("manual_alias")->integer!=0);auto result=c::evaluate(*compiled,input);
  if(fn=="hasTag"||fn=="hasTagGroup"){need(result&&c::truth(*result).value()==(row.find("value")->integer!=0),"Actual original cache/manual/group result differs");++compared;}
  else{need(!result&&result.error().kind==c::ProblemKind::Unavailable,"Peer cache invented named age/count source");++unavailable;}
 }
 need(compared==24&&unavailable==12,"Actual peer function coverage lost");
}
void namedCoordinatesUseDirectMembership() {
 const auto empty = c::NamedTagMetrics::create({});
 need(bool(empty), "Owning empty named metric set");
 auto metrics = std::make_shared<const c::NamedTagMetrics>(*empty);
 for (const auto* call : {u"hasTagAge(\"Generated-1\")", u"tagPosition(\"Generated-1\")",
                         u"tagPosition(\"Generated-1\",2)", u"countTag(\"Generated-1\")"}) {
  auto expression = c::compile(call);
  need(bool(expression), "Compile peer metric");
  const bool count = std::u16string_view(call).starts_with(u"countTag");
  for (const bool cache : {false, true}) {
   auto input = facts(cache, false);
   input.namedTagMetrics = metrics;
   auto value = c::evaluate(*expression, input);
   if (count) {
    need(!value && value.error().kind == c::ProblemKind::Unavailable, "Download cache became a peer roster");
   } else {
    need(value && std::get<std::int64_t>(*value) == -1, "Peer cache changed the download coordinate");
   }
  }

  auto input = facts(true, true);
  input.namedTagMetrics = metrics;
  auto value = c::evaluate(*expression, input);
  need(!value && value.error().kind == c::ProblemKind::Unavailable, "Ambiguous manual alias selected a peer tag");
  for (const auto& [types, membership] : {
        std::pair{4, r::Membership::Present}, std::pair{4, r::Membership::Unknown},
        std::pair{2, r::Membership::Absent}}) {
   auto peer = r::Tag{1, types, u"Generated-1", {}, true, membership};
   input.tagRegistry = *r::Registry::create({{4, {peer}, true}}, true);
   value = c::evaluate(*expression, input);
   need(!value && value.error().kind == c::ProblemKind::Unavailable, "Unproved peer coordinate became numeric");
  }
 }
}
void identitiesBoundsAndAbsentCaches(){
 auto expression=c::compile(u"hasTag(\"Generated-1\")");need(bool(expression),"Cache expression");auto wrong=facts(true,false,false);auto result=c::evaluate(*expression,wrong);need(!result,"Unregistered cache name became a tag");
 r::Tag unrelated;unrelated.id=1;unrelated.taggableTypes=2;unrelated.name=u"Generated-1";unrelated.membership=r::Membership::Absent;wrong.tagRegistry=*r::Registry::create({{3,{unrelated},true}},true);result=c::evaluate(*expression,wrong);need(result&&!c::truth(*result).value(),"Peer cache promoted unrelated manual tag");
 auto input=facts(false,false);input.peerSetTagNames={u"Other"};result=c::evaluate(*expression,input);need(result&&!c::truth(*result).value(),"Foreign cache name matched");
 input=facts(true,false);c::Limits limits;limits.listValues=0;auto bounded=c::compile(u"true",limits);need(bool(bounded),"Bounded pure expression");result=c::evaluate(*bounded,input);need(!result&&result.error().kind==c::ProblemKind::Bounds,"Peer witness list quota ignored");limits={};limits.totalFactUnits=1;bounded=c::compile(u"true",limits);input.tagRegistry.reset();result=c::evaluate(*bounded,input);need(!result&&result.error().kind==c::ProblemKind::Bounds,"Peer witness text quota ignored");
 auto peer=r::Tag{1,4,u"Generated-1",{},true,r::Membership::Unknown};input={};input.tagRegistry=*r::Registry::create({{4,{peer},false}},false);result=c::evaluate(*expression,input);need(!result&&result.error().kind==c::ProblemKind::Unavailable,"Missing partial peer census became a negative");input.peerSetTagNames={u"Generated-1"};result=c::evaluate(*expression,input);need(result&&c::truth(*result).value(),"Source-bound positive cache lost to partial census");
}
}
int main(){try{actualFunctionsRetainDistinctSources();namedCoordinatesUseDirectMembership();identitiesBoundsAndAbsentCaches();}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}std::cout<<"PASS36 actual peer-set function cases:24 cache/manual/group comparisons,12 missing-owner named metrics;direct peer coordinates and identity/resource fences\n";}
