// SPDX-License-Identifier: GPL-2.0-or-later
#include <biglybtc/untagged_tag_policy.hpp>
#include <biglybtc/bencode.hpp>
#include <array>
#include <fstream>
#include <iostream>
#include <stdexcept>
namespace c=biglybtc::core;namespace p=c::tags::untagged;namespace r=c::tags::runtime;
namespace {
void check(bool value,const char* why){if(!value){throw std::runtime_error(why);}}
bool apply(p::Membership decision,bool before){return decision==p::Membership::Keep?before:decision==p::Membership::Add;}
r::Membership member(bool value){return value?r::Membership::Present:r::Membership::Absent;}
r::Truth other(unsigned ordinary,bool u2,bool u3,bool enabled3=true){
 const std::array<p::Entry,4> source{{{false,member(ordinary&1)},{false,member(ordinary&2)},{true,member(u2)},{enabled3,member(u3)}}};
 auto result=p::otherManualTags(source,true);check(bool(result),"Bounded actual source projection");return *result;
}
void actualOriginalCurrentPersistentNonDestroyedSlice(){
 std::ifstream file(UNTAGGED_TAG_ORIGINAL_FIXTURE,std::ios::binary);const std::string bytes{std::istreambuf_iterator<char>(file),{}};auto source=c::Document::parse(bytes);check(bool(source),"Read actual original untagged source");unsigned compared=0,bug=0,total=0;
 for(const auto& row:source->find("rows")->children){++total;if(!row.find("persistent")->integer||row.find("destroyed")->integer){continue;}
  auto ordinary=unsigned(row.find("ordinary_mask")->integer);const auto saved=unsigned(row.find("saved_mask")->integer);const auto event=row.find("event")->bytes;
  bool a=apply(p::project(other(ordinary,saved&1,saved&2),true,saved&1),saved&1);bool b=apply(p::project(other(ordinary,saved&1,saved&2),true,saved&2),saved&2);
  check((ordinary|(a?4u:0u)|(b?8u:0u))==unsigned(row.find("initial")->integer),"Current original initial untagged membership differs");
  if(event=="disable-reenable-remove0"){
   if(ordinary==3&&saved==0){check(row.find("after")->integer==6&&row.find("count")->integer==-1,"Actual original listener lifecycle counterexample changed");
    // Source B remains present. A current snapshot must not add Untagged,
    // regardless of the original stale accumulated listener count.
    check(other(2,true,false,false)==r::Truth::Yes&&p::project(r::Truth::Yes,true,true)==p::Membership::Remove,"Native correction trusted an obsolete listener counter");++bug;}
   continue;
  }
  if(event=="add0"||event=="remove-add0"){ordinary|=1;}
  else if(event=="remove0"||event=="add-remove0"){ordinary&=~1u;}
  else if(event=="remove-all"){ordinary=0;}
  const bool enabled3=event!="disable3";if(!enabled3){b=false;}
  const auto known=other(ordinary,a,b,enabled3);a=apply(p::project(known,true,a),a);if(enabled3){b=apply(p::project(known,true,b),b);}
  check((ordinary|(a?4u:0u)|(b?8u:0u))==unsigned(row.find("after")->integer),"Current persistent untagged outcome differs from actual original");++compared;
 }check(total==512&&compared==112&&bug==1,"Actual original source qualification count changed");
}
void incompleteOrUnownedSourcesCannotInventAbsence(){
 const std::array<p::Entry,3> positive{{{true,r::Membership::Present},{std::nullopt,r::Membership::Unknown},{false,r::Membership::Present}}};auto result=p::otherManualTags(positive,false);check(result&&*result==r::Truth::Yes,"Unknown source hid a real positive ordinary witness");
 const std::array<p::Entry,3> unknown{{{true,r::Membership::Present},{false,r::Membership::Unknown},{false,r::Membership::Absent}}};result=p::otherManualTags(unknown,true);check(result&&*result==r::Truth::Unknown,"Unknown membership was counted as no other tag");
 const std::array<p::Entry,1> role{{{std::nullopt,r::Membership::Present}}};result=p::otherManualTags(role,true);check(result&&*result==r::Truth::Unknown,"Unknown tag role counted as a known ordinary member");
 result=p::otherManualTags({},false);check(result&&*result==r::Truth::Unknown,"Empty partial census became a negative");result=p::otherManualTags({},true);check(result&&*result==r::Truth::No,"Known empty census not represented");
 for(bool current:{false,true}){check(p::project(r::Truth::Unknown,true,current)==p::Membership::Keep,"Unknown source changed membership");check(p::project(r::Truth::No,false,current)==p::Membership::Keep,"Nonpersistent current source manufactured a new untagged member");}
 std::vector<p::Entry> oversized(4097);check(!p::otherManualTags(oversized,true),"Source quota was truncated into actionable absence");
}
}
int main(){try{actualOriginalCurrentPersistentNonDestroyedSlice();incompleteOrUnownedSourcesCannotInventAbsence();}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}std::cout<<"PASS untagged policy:112 actual persistent original comparisons,512-row source,listener-lifecycle counterexample and conservative unknowns\n";return 0;}
