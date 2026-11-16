// SPDX-License-Identifier: GPL-2.0-or-later
#include <biglybtc/tag_constraints.hpp>
#include <biglybtc/bencode.hpp>
#include <algorithm>
#include <fstream>
#include <iostream>
#include <stdexcept>
namespace c=biglybtc::core::tags::constraints;namespace r=biglybtc::core::tags::runtime;namespace b=biglybtc::core;
namespace {void require(bool ok,const char* why){if(!ok){throw std::runtime_error(why);}}
c::Text text(std::string_view s){c::Text out;for(unsigned char v:s){require(v<128,"Source fixture expected ASCII");out+=char16_t(v);}return out;}
void originalSymbolicDependencies(){
 std::ifstream input(COMPILED_TAG_DEPENDENCIES_FIXTURE,std::ios::binary);const std::string bytes{std::istreambuf_iterator<char>(input),{}};auto doc=b::Document::parse(bytes);require(bool(doc),"Actual original compiled dependency fixture");unsigned cases=0,valid=0,invalid=0,syntax=0;
 r::Type type;type.id=3;type.complete=true;for(int id=0;id<4;++id){r::Tag tag;tag.id=id;tag.taggableTypes=2;tag.name=id==0?u"Root":id==2?u"Beta":u"Alpha";tag.membership=r::Membership::Absent;type.tags.push_back(std::move(tag));}auto registry=r::Registry::create({type},true);require(bool(registry),"Generated actual manual-name resolver");
 for(const auto& row:doc->find("rows")->children){auto expression=c::compile(text(row.find("source")->bytes));if(!expression){require(!row.find("valid")->integer&&expression.error().kind==c::ProblemKind::Invalid,"Native refused a valid original source");++cases;++invalid;++syntax;continue;}std::vector<c::Text> names(expression->tagDependencies().begin(),expression->tagDependencies().end());std::ranges::sort(names);
  if(row.find("valid")->integer){std::vector<c::Text> expected;for(const auto& value:row.find("names")->children){expected.push_back(text(value.bytes));}std::ranges::sort(expected);require(names==expected,"Symbolic dependency names differ from actual original constructor");std::vector<std::int64_t> ids;for(const auto& name:names){auto found=registry->lookupName(name);require(found&&found->tags.size()==1,"Original first-per-type name lookup");ids.push_back(found->tags[0].id);}std::ranges::sort(ids);require(ids.size()==row.find("ids")->children.size(),"Resolved dependency count differs");for(std::size_t i=0;i<ids.size();++i){require(ids[i]==row.find("ids")->children[i].integer,"Duplicate-name resolution differs from Java");}++valid;
  }else{require(!expression->requiredTagNames().empty(),"Unknown eager reference was dropped");c::Facts facts;facts.tagRegistry=*registry;auto evaluated=c::evaluate(*expression,facts);require(!evaluated&&evaluated.error().kind==c::ProblemKind::Unavailable,"Unknown original compile-time name escaped eager admission guard");++invalid;}++cases;
 }require(cases==16&&valid==12&&invalid==4&&syntax==1,"Original dependency case census changed");require(c::Expression{}.tagDependencies().empty()&&c::Expression{}.requiredTagNames().empty(),"Invalid expression invented metadata");
 auto source=c::compile(u"hasTag(\"Alpha\")");auto retained=*source;source=c::Expression{};require(retained.tagDependencies().size()==1&&retained.tagDependencies()[0]==u"Alpha","Immutable copied expression lost dependency metadata");
}
}
int main(){try{originalSymbolicDependencies();}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}std::cout<<"PASS16 actual original compiled dependency cases:12 resolved/1 parameter refusal/3 eager unknown,first-per-type duplicates and immutable metadata\n";return 0;}
